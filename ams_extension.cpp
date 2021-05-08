/*
* AMSAT BEACON EXTENSION
* ======================
* DJ0ABR 4.2021
* 
* LINUX ONLY !!!
* 
* HSmodem läuft im Bakenbetrieb ohne GUI.
* Das GUI ist jedoch erforderlich um HSmodem Konfigurationsdaten zu senden
* und macht außer dem einen IsAlive-Check.
* 
* Dazu werden regelmäßig Broadcast Messages zu HSmodem gesendet.
* 
* Dieses Programm simuliert diese Broadcast Message und wird daher
* auch benutzt um HSmodem zu konfigurieren
* 
*/


#include "../hsmodem/hsmodem.h"

#ifdef AMS_TESTMODE

void* amsproc(void* param);
void ams_bc_rxdata(uint8_t* pdata, int len, struct sockaddr_in* rxsock);
void ams_pluto_rxdata(uint8_t* pdata, int len, struct sockaddr_in* rxsock);
void* dxcproc(void* param);
char* getConfigElement(char* elemname);

int ams_sock, ams_plutosock;
uint32_t fifostatus;
char *mylocalIP;

void ams_BCsimulation()
{
    // read info from config file
    char* pv = getConfigElement("BULLETIN_DIRECTORY");
    if (pv && strlen(pv))
    {
        strncpy(AUTOSENDFOLDER, pv, sizeof(AUTOSENDFOLDER) - 1);
        printf("bulletin directory: %s\n", AUTOSENDFOLDER);
    }

    pv = getConfigElement("BULLETIN_CYCLETIME");
    if (pv && strlen(pv))
    {
        int tim = atoi(pv);
        if (tim > 0)
            filesend_delay_seconds = tim;
        printf("File send cycle time: %d s\n", filesend_delay_seconds);
    }

    // UDP rx for responses to BC messages from HSmodem
    UdpRxInit(&ams_sock, 40133, &ams_bc_rxdata, &keeprunning);

    // UDP rx for status messages from Pluto driver
    UdpRxInit(&ams_plutosock, 40810, &ams_pluto_rxdata, &keeprunning);

	mylocalIP = ownIP();
	if (mylocalIP == NULL || strlen(mylocalIP) < 7)
	{
		printf("cannot evaluate local IP of this computer\n");
		exit(0);
	}

    pthread_t ams_pid;
    int ret = pthread_create(&ams_pid, NULL, amsproc, NULL);
    if (ret)
    {
        printf("Amsat Beacon: process NOT started !!!\n\r");
        exit(0);
    }

    pthread_t dxc_pid;
    ret = pthread_create(&dxc_pid, NULL, dxcproc, NULL);
    if (ret)
    {
        printf("DX Cluster: process NOT started !!!\n\r");
        exit(0);
    }
}

uint8_t* getBCconfigdata(int* plen)
{
    static uint8_t txdata[1000];
    memset(txdata, 0, 1000);

    txdata[0] = 0x3c;       // fixed ID value, do not change
    txdata[1] = 50;         // initial TX volume in %
    txdata[2] = 50;         // unused: initial RX volume in %
    txdata[3] = 10;         // usually unused: do voice announcement every n transmissions
    txdata[4] = 50;         // unused: initial voice RX volume in %
    txdata[5] = 50;         // unused: initial voice TX volume in %
    txdata[6] = 1;          // number of frame retransmissions
    txdata[7] = 0;          // send prerecorded audio file: off
    txdata[8] = 0;          // unused: RTTY
    txdata[9] = 9;          // mode 8APSK 7200 bps
    txdata[10] = 1;          // activate external data interface for WB/NB, CWskimmer and DXcluster
    strcpy((char *)(txdata + 20), "Sound not used");
    strcpy((char*)(txdata + 120), "Sound not used");
    strcpy((char*)(txdata + 220), "DK0SB");
    strcpy((char*)(txdata + 240), "JO31OK");
    strcpy((char*)(txdata + 250), "Amsat-DL Bochum");

    *plen = 270;

    return txdata;
}

void* amsproc(void* param)
{
    pthread_detach(pthread_self());

    printf("Amsat Beacon Thread running\n");

    while (keeprunning)
    {
        int len = 0;
        uint8_t* txdata = getBCconfigdata(&len);
        sendUDP(mylocalIP, 40131, txdata, len);
        usleep(1000000);
    }

    pthread_exit(NULL); // self terminate this thread
    return NULL;
}

// receive responses to BC messages from HSmodem
void ams_bc_rxdata(uint8_t* pdata, int len, struct sockaddr_in* rxsock)
{
    //printf("AMS: BC response received. Len:%d\n", len);
    if (pdata[0] == 3)
    {
        pdata[len - 1] = 0; // terminate string
        //printf("Audio Devices String:\n%s\n", (char *)(pdata + 5));
    }
    else
    {
        // other GUI messages, not used here
    }
}

// receive status messages from Pluto driver
void ams_pluto_rxdata(uint8_t* pdata, int len, struct sockaddr_in* rxsock)
{
    fifostatus = pdata[0];
    fifostatus <<= 8;
    fifostatus += pdata[1];
    fifostatus <<= 8;
    fifostatus += pdata[2];
    fifostatus <<= 8;
    fifostatus += pdata[3];

    static uint32_t oldstat = -1;
    if (oldstat != fifostatus)
    {
        //printf("Pluto Fifo Fillstatus: %d\n", fifostatus);
        oldstat = fifostatus;
    }
}

int getAMSfifostat()
{
    return fifostatus;
}

/*
* bei 8APSK 6000 hat 2000 Samples/s
* es ist bereits ein TX Interpolator von 24 drin
* dadurch kommt es hier mit 48kS/s an
* 
* Pluto:
* Samplerate: 3,072 MS/s
* TX(USP) Interpolation: 64
* ergibt 3,072M/64 = 48kS/s
*/

uint32_t testcnt = 0;

// send samples via UDP to pluto driver
// sample ... complex sample, output of the modulator
//            baseband with 2400 Samples/s
#define SAMPperFRAME    7500
#define MAXSIGNED32BIT  2000000000  // no need for more
#define MAXSIGNEDINT    2147483647  // no need for more
void ams_testTX(liquid_float_complex sample)
{
    // send to pluto in this format
    // Byte 0..3: real part as 32bit signed int, LSB first
    // Byte 4..7: imag part as 32bit signed int, LSB first
    // and so on
    // length of a frame: 2400*8 = 19200 Byte, which is 1s of samples

    static int32_t re[SAMPperFRAME];
    static int32_t im[SAMPperFRAME];
    static int idx = 0;

    sample.real *= 0.9;
    sample.imag *= 0.9;
    if (sample.real > 1.0 || sample.real < -1.0) printf("real: %f\n", sample.real);
    if (sample.imag > 1.0 || sample.imag < -1.0) printf("imag: %f\n", sample.imag);

    // convert values to int32
    re[idx] = (int32_t)(sample.real * (float)MAXSIGNED32BIT);
    im[idx] = (int32_t)(sample.imag * (float)MAXSIGNED32BIT);

    if (++idx >= SAMPperFRAME)
    {
        int to = 0;
        while (1)
        {
            if (fifostatus < 5) break;
            sleep_ms(1);
            if (++to > 1000)
            {
                printf("Timeout while waiting for Pluto fifo\n");
                break;
            }
        }

        // convert 32 bit values to byte stream
        uint8_t txbuf[SAMPperFRAME * 8];
        int di = 0;
        for (int i = 0; i < SAMPperFRAME; i++)
        {
            if (re[i] >= (MAXSIGNEDINT - 1) || re[i] <= (-MAXSIGNEDINT + 1)) printf("check re[i]: %d\n", re[i]);
            if (im[i] >= (MAXSIGNEDINT - 1) || im[i] <= (-MAXSIGNEDINT + 1)) printf("check im[i]: %d\n", im[i]);

            txbuf[di++] = re[i] & 0xff;
            txbuf[di++] = re[i] >> 8;
            txbuf[di++] = re[i] >> 16;
            txbuf[di++] = re[i] >> 24;
            txbuf[di++] = im[i] & 0xff;
            txbuf[di++] = im[i] >> 8;
            txbuf[di++] = im[i] >> 16;
            txbuf[di++] = im[i] >> 24;
        }

        //showbytestring("tx:", txbuf, SAMPperFRAME * 8, 30);
        sendUDP("192.168.20.99", 40809, txbuf, SAMPperFRAME * 8);

        idx = 0;
    }
}

// ============ DX Cluster ====================

int telnet_main(char* ip, char* port);

void* dxcproc(void* param)
{
    pthread_detach(pthread_self());

    printf("DX ClusterThread running\n");

    telnet_main("dx.w1nr.net", "23");

    pthread_exit(NULL); // self terminate this thread
    return NULL;
}

void FormatAndSendMessage(char* msg);

#define BUFFER_SZ 1000
#define DEBUG 0

char dxc_rxbuffer[BUFFER_SZ + 10];
int rxidx = 0;

int maximum(int a, int b) {
	return a > b ? a : b;
}

void handle_data_just_read(int sck_fd, char* data, int num_bytes) {
	char out_buf[BUFFER_SZ];
	int out_ptr = 0;

	char IAC = (char)255; //IAC Telnet byte consists of all ones

	rxidx = 0;
	memset(dxc_rxbuffer, 0, sizeof(dxc_rxbuffer));
	for (int i = 0; i < num_bytes; i++) {
		if (data[i] == IAC) {
			//command sequence follows
			if (DEBUG) { printf("IAC detected\n"); }
			out_buf[out_ptr++] = IAC;
			i++;
			switch (data[i]) {
			case ((char)251):
				//WILL as input
				if (DEBUG) { printf("WILL detected\n"); }
				out_buf[out_ptr++] = (char)254; //DON'T, rejecting WILL
				i++;
				out_buf[out_ptr++] = data[i];
				break;
			case ((char)252):
				//WON'T as input
				if (DEBUG) { printf("getting won't as input, weird!\n"); }
				break;
			case ((char)253):
				//DO as input
				if (DEBUG) { printf("DO detected\n"); }
				out_buf[out_ptr++] = (char)(char)252; //WON'T, rejecting DO
				i++;
				out_buf[out_ptr++] = data[i];
				break;
			case ((char)254):
				//DON'T as input
				if (DEBUG) { printf("getting don't as input, weird!\n"); }
				break;
			default:
				//not a option negotiation, a genereal TELNET command like IP, AO, etc.
				if (DEBUG) { printf("general TELNET command %x\n", (int)data[i]); }
				break;
			}
		}
		else {
			//is data
			//printf("%c", data[i]);
			dxc_rxbuffer[rxidx] = data[i];
			if (rxidx < BUFFER_SZ) rxidx++;
		}
	}

	fflush(stdout); //need to flush output here as printf doesn't do so unless '\n' is encountered, and we're printing char-by-char the real data

	if (write(sck_fd, out_buf, out_ptr) == -1) {
		printf("error write()'ing\n");
		exit(0);
	}

	if (DEBUG) { printf("packet sent!\n\n"); }
}

void start_communication(int sck_fd) {
	char inp_buf[BUFFER_SZ];
	//char* user_command = NULL;
	//size_t user_max_length = 0;

	fd_set fds;
	FD_ZERO(&fds);
	int mx;
	while (1) {

		//FD_SET(fileno(stdin), &fds);
		FD_SET(sck_fd, &fds);
		//mx = maximum(fileno(stdin) + 1, sck_fd + 1);
		mx = sck_fd + 1;

		select(mx, &fds, NULL, NULL, NULL); //IO Multiplexing

		if (FD_ISSET(sck_fd, &fds)) {
			//read from TCP socket
			int num_read = read(sck_fd, inp_buf, BUFFER_SZ);
			if (DEBUG) { printf("reading from socket(%d bytes read)\n", num_read); }
			if (num_read == -1) {
				printf("error read()'ing\n");
				exit(0);
			}
			else if (num_read == 0) {
				printf("EOF reached, connection closed?\n");
				exit(0);
			}
			handle_data_just_read(sck_fd, inp_buf, num_read);

			int len = strlen(dxc_rxbuffer);
			if (len < 100 && len > 50)
			{
				if (dxc_rxbuffer[len - 1] == '\n') dxc_rxbuffer[len - 1] = 0;
				printf("DX Message:%d %s\n", (int)strlen(dxc_rxbuffer), dxc_rxbuffer);
				FormatAndSendMessage(dxc_rxbuffer);
			}
			else
			{
				printf("Management Message: %s\n", dxc_rxbuffer);
			}

			if (strstr(dxc_rxbuffer, "login:") && strstr(dxc_rxbuffer, "USING THEIR OWN CALLSIGNS ONLY"))
			{
				printf("SEND LOGIN\n");
				char t[1000];
				strcpy(t, "DK0SB\n");
				int ret = write(sck_fd, t, strlen(t));
				if (ret) {}
			}
		}
	}
}

int seekFirstChar(char* msg)
{
	for (unsigned int i = 0; i < strlen(msg); i++)
		if (msg[i] > ' ') return i;

	return -1;
}

int seekLastChar(char* msg)
{
	for (unsigned int i = strlen(msg) - 1; i >= 0; i--)
		if (msg[i] > ' ') return i;

	return 0;
}

void getText(char* _dst, char* src, unsigned int start, unsigned int end)
{
	char msg[200];

	if (end >= strlen(src))
	{
		*_dst = 0;
		return;
	}

	int idx = 0;
	for (unsigned int i = start; i < end; i++)
	{
		if (i >= strlen(src)) break;
		msg[idx++] = src[i];
	}
	msg[idx] = 0;

	// remove spaces
	if (strlen(msg) > 0)
	{
		idx = 0;
		int fst = seekFirstChar(msg);
		if (fst != -1)
		{
			int lst = seekLastChar(msg);
			memcpy(_dst, msg + fst, lst - fst + 1);
			_dst[lst - fst + 1] = 0;
		}
		else
			*_dst = 0;
	}
	else
		*_dst = 0;
}

void FormatAndSendMessage(char* msg)
{
	/* UDP Data format(Packet Length : 219 Byte, fits into one HSmodem payload)
	 * ========================================================================
	 * Byte 0 ... Data Type
	 * Byte 1 ... Length
	 * Byte 2 - 218 ..data(217 Bytes)
	*/
	int len = strlen(dxc_rxbuffer);
	if (len > 217) len = 217;

	uint32_t extDataID = 0x7743fa9f;
	uint8_t ubuf[219];
	memset(ubuf, 0, sizeof(ubuf));

	ubuf[0] = extDataID >> 24;
	ubuf[1] = extDataID >> 16;
	ubuf[2] = extDataID >> 8;
	ubuf[3] = extDataID & 0xff;

	ubuf[4] = 0; // DX cluster message
	ubuf[5] = len;

	// DX de IZ3HLJ: 10489500.0  CO8LY        TNX QSO 73                     1346Z JN65
	// 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234
	// 0         1         2         3         4         5         6         7         8

	char reporter[30];
	char qrg[30];
	char dxcall[30];
	char message[100];
	char utctime[6];
	char qthloc[5];

	getText(reporter, dxc_rxbuffer, 6, 14);
	getText(qrg, dxc_rxbuffer, 14, 26);
	getText(dxcall, dxc_rxbuffer, 26, 39);
	getText(message, dxc_rxbuffer, 39, 70);
	getText(utctime, dxc_rxbuffer, 70, 76);
	getText(qthloc, dxc_rxbuffer, 76, 80);

	// %.n : text has max n length, if longer it will be truncuated
	sprintf((char*)ubuf + 6, "%.20s~%.20s~%.20s~%.40s~%.20s~%.20s", reporter, qrg, dxcall, message, utctime, qthloc);

	printf("<%s>\n", (char*)(ubuf + 6));

	sendUDP(mylocalIP, 40135, ubuf, 219);
}

int telnet_main(char* ip, char* port)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;
	struct addrinfo* results;

	if (getaddrinfo(ip, port, &hints, &results) != 0) {
		printf("error getaddrinfo()'ing, translation error\n");
		return 0;
	}

	struct addrinfo* addr_okay;
	int succeeded = 0;
	int sck_fd = socket(AF_INET, SOCK_STREAM, 0);
	for (addr_okay = results; addr_okay != NULL; addr_okay = addr_okay->ai_next) {
		if (connect(sck_fd, addr_okay->ai_addr, addr_okay->ai_addrlen) >= 0) {
			succeeded = 1;
			//printf("connected successfully!\n");
			break;
		}
	}
	freeaddrinfo(results);

	if (!succeeded) {
		printf("error, coudln't connect() to any of possible addresses\n");
		return 0;
	}

	start_communication(sck_fd);
	close(sck_fd);

	return 0;
}

#endif
