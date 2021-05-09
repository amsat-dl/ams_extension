/*
* AMSAT BEACON EXTENSION
* ======================
* DJ0ABR 4.2021
* 
* LINUX ONLY !!!
* 
* HSmodem l‰uft im Bakenbetrieb ohne GUI.
* Das GUI ist jedoch erforderlich um HSmodem Konfigurationsdaten zu senden
* und macht auﬂer dem einen IsAlive-Check.
* 
* Dazu werden regelm‰ﬂig Broadcast Messages zu HSmodem gesendet.
* 
* Dieses Programm liest das zentrale Baken-Configfile das zur Konfiguration
* der Bake benutzt wird
* 
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// config file location
// enter directory and name of the configuration file
#define CONFIGFILE		"~/mmbeacon/hsmodemLinux/ams_config.txt"

// ============= read Beacon Config file ===================

char* cleanStr(char* s)
{
    if (s[0] > ' ')
    {
        // remove trailing crlf
        for (size_t j = 0; j < strlen(s); j++)
            if (s[j] == '\n' || s[j] == '\r') s[j] = 0;
        return s;    // nothing to do
    }

    for (size_t i = 0; i < strlen(s); i++)
    {
        if (s[i] >= '0')
        {
            // i is on first character
            memmove(s, s + i, strlen(s) - i);
            s[strlen(s) - i] = 0;
            // remove trailing crlf
            for (size_t j = 0; j < strlen(s); j++)
                if (s[j] == 'n' || s[j] == '\r') s[j] = 0;
            return s;
        }
    }
    return NULL;   // no text found in string
}

char* getword(char* s, int idx)
{
    if (idx == 0)
    {
        for (size_t i = 0; i < strlen(s); i++)
        {
            if (s[i] < '0')
            {
                s[i] = 0;
                return s;
            }
        }
        return NULL;
    }

    for (size_t j = 0; j < strlen(s); j++)
    {
        if (s[j] > ' ')
        {
            char* start = s + j;
            for (size_t k = 0; k < strlen(start); k++)
            {
                if (start[k] == ' ' || start[k] == '\r' || start[k] == '\n')
                {
                    start[k] = 0;
                    return start;
                }
            }
            return start;
        }
    }

    return NULL;
}

// read the value of an element from the config file
// Format:
// # ... comment
// ElementName-space-ElementValue
// the returned value is a static string and must be copied somewhere else
// before this function can be called again
char* getConfigElement(char* elemname)
{
    static char s[501];
    int found = 0;
    char fn[1024];
    
    if(strlen(CONFIGFILE) > 512)
    {
        printf("config file path+name too long: %s\n",CONFIGFILE);
        exit(0);
    }
    strcpy(fn,CONFIGFILE);
    
    if(fn[0] == '~')
    {
        struct passwd *pw = getpwuid(getuid());
        const char *homedir = pw->pw_dir;
        sprintf(fn,"%s%s",homedir,CONFIGFILE+1);
    }

    printf("read Configuration file %s\n",fn);
    FILE *fr = fopen(fn,"rb");
    if(!fr) 
    {
        printf("!!! Configuration file %s not found !!!\n",fn);
        exit(0);
    }

    while (1)
    {
        if (fgets(s, 500, fr) == NULL) break;
        // remove leading SPC or TAB
        if (cleanStr(s) == 0) continue;
        // check if its a comment
        if (s[0] == '#') continue;
        // get word on index
        char* p = getword(s, 0);
        if (!p) break;
        if (strcmp(p, elemname) == 0)
        {
            char val[500];
            if (*(s + strlen(p) + 1) == 0) continue;
            p = getword(s + strlen(p) + 1, 1);
            if (!p) break;
            // replace , with .
            char* pkomma = strchr(p, ',');
            if (pkomma) *pkomma = '.';
            strcpy(val, p);
            strcpy(s, val);
            found = 1;
            break;
        }

    }

    fclose(fr);
    if (found == 0) return NULL;
    return s;
}
