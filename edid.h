#pragma once
#include "display.h"
#include <string.h>
unsigned read_edid(const wchar_t *device, BYTE *out, unsigned capacity);

static bool detailed_match(const BYTE *d, const DisplayPreset *p)
{
    unsigned clock=d[0]+256u*d[1], x=d[2]+((d[4]&240)<<4), y=d[5]+((d[7]&240)<<4);
    if(!clock || x!=p->width || y!=p->height || (d[17]&128)) return false;
    unsigned xt=x+d[3]+((d[4]&15)<<8), yt=y+d[6]+((d[7]&15)<<8);
    double hz=10000.0*clock/(xt*yt);
    return hz>p->hz-0.5 && hz<p->hz+0.5;
}

static bool vic_match(unsigned vic,const DisplayPreset *p)
{
    return (vic==16 && p->width==1920 && p->hz==60) ||
           (p->width==3840 && ((vic==95 && p->hz==30) || (vic==97 && p->hz==60)));
}

/* A negative result means no matching entry in the decoded timing formats. */
static const wchar_t *edid_status(const BYTE *data,unsigned blocks,const DisplayPreset *p)
{
    static const BYTE header[]={0,255,255,255,255,255,255,0};
    if(!blocks || memcmp(data,header,8)) return L"n/d";
    bool partial=blocks!=1u+data[126];
    for(unsigned i=0;i<blocks;i++) {
        const BYTE *b=data+128*i;
        unsigned sum=0;
        for(unsigned j=0;j<128;j++) sum+=b[j];
        if(sum%256) { if(!i) return L"n/d"; partial=true;continue; }
        unsigned start=54;
        if(i) {
            if(b[0]!=2) {partial=true;continue;}
            start=b[2];
            if(!start) continue;
            if(start<4 || start>127) {partial=true;continue;}
            for(unsigned j=4;j<start;) {
                unsigned tag=b[j]>>5, len=b[j++]&31, end=j+len;
                if(end>start) {partial=true;break;}
                bool only420=tag==7 && len && b[j]==14;
                if(tag==2 || only420)
                    for(unsigned k=j+only420;k<end;k++) {
                        unsigned vic=b[k];if(vic>=129 && vic<=192) vic-=128;
                        if(vic_match(vic,p)) return L"sì";
                    }
                j=end;
            }
        } else {
            for(unsigned j=38;j<54;j+=2) {
                unsigned x=(b[j]+31u)*8,ratio=b[j+1]>>6;
                if(x==p->width && ratio==3 && x*9==p->height*16 && (b[j+1]&63)+60u==p->hz) return L"sì";
            }
        }
        for(unsigned j=start;j+18<=(i?127u:126u);j+=18)
            if(detailed_match(b+j,p)) return L"sì";
    }
    return partial ? L"incompleto" : L"non rilevato";
}
