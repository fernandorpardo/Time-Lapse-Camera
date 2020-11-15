#ifndef HTTPPOST_HEADER_FILLE_H
#define HTTPPOST_HEADER_FILLE_H
#endif


//#define HTTPPOST_DEBUG_ENABLED


void hhtpPOST_init(const char *, const char *, unsigned int );
int hhtpPOST_upload(char *, size_t, double *, char ** );
size_t hhtpPOST_header(const char*, char *, size_t , size_t *, size_t );

/* END OF FILE */