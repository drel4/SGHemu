#include "jar.h"
#include <stdio.h>
int main(int argc,char **argv)
{
    JarPackage j={0};
    if(argc!=2){fprintf(stderr,"Usage: jarinspect GAME.jar\n");return 2;}
    if(!jar_open(&j,argv[1])){fprintf(stderr,"%s\n",j.error);jar_release(&j);return 1;}
    printf("Name: %s\nVendor: %s\nVersion: %s\nMIDlet: %s\nMIDP: 1.0\nCLDC: 1.0\nBytes: %lu\n"
        "Package preflight passed; this does not prove installation or execution.\n",
        j.name,j.vendor,j.version,j.class_name,(unsigned long)j.size);
    jar_release(&j);return 0;
}
