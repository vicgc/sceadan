/*============================================================
 *
 * Copyright (c) 2012-2013 The University of Texas at San Antonio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Revision History:
 *
 * 2014 - substantially rewritten by Simson L. Garfinkel
 *        - Works with precompiled model (avoids model load times)
 *        - Smaller, cleaner code base.
 *        - sceadan now has a clear API that is separated from file-reading.
 * 
 * 2013 - cleaned by Lishu Liu, 
 *
 * 2013 - Original version by:
 *        Dr. Nicole Beebe Department of Information Systems
 *        and Cyber Security (nicole.beebe@utsa.edu)
 *        University of Texas at San Antonio,
 *        One UTSA Circle 
 *        San Antonio, Texas 78209
 *    
 *        Laurence Maddox, Department of Computer Science
 *
 *
 ************************************************************/

#include "config.h"
#include <ctype.h>
#include <inttypes.h>
#include <assert.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include "sceadan.h"

/* Globals for the stand-alone program */

size_t block_size = 512;
int    opt_json = 0;
int    opt_train = 0;
int    opt_omit = 0;
int    opt_blocks = 0;
int    opt_help = 0;
int    opt_preport  = 0;        /* report percentage sampled to pfd */
int    opt_percentage = 100;
int    opt_seed = 0;                    /* random number seed */
int    opt_reduce = 0;          /* top n feature to select while doing feature reduction */

const char *feature_mask_file_in  = 0;
const char *feature_mask_file_out = 0;
const char *opt_class_file = 0;
const char *opt_model = 0;

static sceadan *s = 0;                         /* the sceadan we are using */

static void do_output(sceadan *sc,const char *path,uint64_t offset,int file_type )
{
    printf("%-10" PRId64 " %s # %s\n", offset,sceadan_name_for_type(sc,file_type),path);
}



/**
 * ftw() callback to process a file. In this implementation it handles the file whole or block-by-block, prints
 * results with do_output (above), and then returns.
 */
static int process_file(const char path[],
                        const struct stat *const sb, /* ignored */
                        const int typeflag ) 
{
    if(typeflag!=FTW_F){
        return 0;
    }

    /* To get consistent random numbers, but random numbers that are different for every file,
     * seed the random number generator and then exercise it a specific number of times that is
     * determined by the file name.
     */
    if(opt_percentage!=100){
        srandom(opt_seed);                  /*  */
        for(int i=0;path[i];i++){
            for(int j=0;j<path[i];j++){
                random();
            }
        }
    }

    int training = 0;
        
    if(opt_json){
        sceadan_dump_json_on_classify(s,opt_json,stdout);
        training = 1;
    }

    if(opt_train){
        sceadan_dump_nodes_on_classify(s,opt_train,stdout);
        training = 1;
    }

    /* Test the incremental classifier */
    const int fd = (strcmp(path,"-")==0) ? STDIN_FILENO : open(path, O_RDONLY|O_BINARY);
    if (fd<0){ perror("open");exit(0); }
    uint8_t   *buf = (uint8_t *)malloc(block_size);
    if(buf==0){ perror("malloc"); exit(1); }
        
    /* Read the file one block at a time */
    uint64_t offset = 0;
        
    /* See if we are supposed to skip the first block */
    if(opt_omit){
        lseek(fd,block_size,SEEK_SET);
        offset += block_size;
    }
    while(true){
        const ssize_t rd = read(fd, buf, block_size);
        if(rd==-1){ perror("read"); exit(0);}
        /* if we read data, update the vectors */
        if(rd>0){
            sceadan_update(s,buf,rd); 
        }
        /* Print the results if we are classifying each block and we read a complete block,
         * or if we are not classifying each block and we read nothing (meaning we are at the end)
         *
         * sceadan_classify() clears the vectors.
         */

        if((opt_blocks && rd==block_size) ||
           (!opt_blocks && rd==0)){

            int process_block = (random() % 100) < opt_percentage;
            if(process_block){
                int t = sceadan_classify(s);
                uint64_t start  = opt_blocks ? offset : 0;
                if(!training) do_output(s,path,start,t); /* print results if not producing vectors for training*/
                if(opt_preport) fprintf(stderr,"%" PRIu64 "-%" PRIu64 "\n",offset,offset+rd);
            }
        }
        /* If we read nothing, break out of the loop */
        if(rd==0) break;
        offset += rd;
    }
    free(buf);
    sceadan_clear(s);
    if(fd) close(fd);
    return 0;
}

#define FTW_MAXOPENFD 8
static void process_dir( const          char path[])
{
    ftw (path, &process_file, FTW_MAXOPENFD);
}


static int alldigits(const char *str)
{
    while(*str){
        if(isdigit(*str)==0) return 0;
        str++;
    }
    return 1;
}



static int type_for_name(const char *name)
{
    if (alldigits(name)) return atoi(name);
    sceadan *sc = sceadan_open(0,opt_class_file,0);
    int ival = sceadan_type_for_name(sc,name);
    sceadan_close(sc);
    if(ival>0) return ival;
    fprintf(stderr,"%s: not a valid type name\n",name);
    exit(1);
}

static const char *name_for_type(int n)
{
    sceadan *sc = sceadan_open(0,opt_class_file,0);
    const char *ret = sceadan_name_for_type(sc,n);
    sceadan_close(sc);
    return ret;
}

void usage(void) __attribute__((noreturn));
void usage()
{
    puts("usage: sceadan_app [options] inputfile [file2 file3 ...]");
    puts("where [options] are:");
    printf("infile - file to analyze. Specify '-' to input from stdin\n");
    printf("for training:\n");
    printf("  -j <class>  - generate features for <class> and output in JSON format\n");
    printf("  -t <class>  - generate a liblinear training for class <class>\n");
    printf("  -P          - report the blocks and byte ranges sampled to stderr\n");
    printf("  -s N        - specifies a random number generator seed.\n");
    printf("  -x          - omit file headers (the first block)\n");
    printf("  -p 0-100    - specifies the percentage of blocks to sample (default 100)\n");
    printf("  -n M        - ngram mode (0=disjoint, 1=overlapping, 2=even/odd)\n");
    printf("  -R n        - reduce feature by selecting top 'n' features based on feature weight.\n");
    printf("  -F <feature_mask_file> - feature mask file name for output.\n");

    printf("\nfor classifying:\n");
    printf("  -m <modelfile>   - use modelfile instead of build-in model\n");
    
    printf("\ngeneral:\n");
    printf("  -C classfile  - Specify a file of user-defined class types (one type per line)\n");
    printf("  -T [#|name|-] - If #, provide the sceadan type name; if name, provide the type number; if -, list\n");
    printf("  -b <size>   - specifies blocksize (default %zd) for block-by-block classification.\n",block_size);
    printf("  -h          - generate help (-hh for more)\n");
    printf("  -f <feature_mask_file> - feature mask file name for input.\n");

    puts("");
    if(opt_help>1){
        puts("Classes");
        sceadan *sc = sceadan_open(0,opt_class_file,0);
        for(int i=0;sceadan_name_for_type(sc,i);i++){
            printf("\t%2d : %s\n",i,sceadan_name_for_type(sc,i));
        }
        sceadan_close(sc);
    }
    exit(0);
}

int main (int argc, char *const argv[])
{
    int ch;
    int opt_ngram_mode = SCEADAN_NGRAM_MODE_DEFAULT;

    while((ch = getopt(argc,argv,"b:C:ef:F:j:m:n:Pp:R:r:T:t:xh")) != -1){
        switch(ch){
        case 'C': opt_class_file = optarg; break;
        case 'b': block_size = atoi(optarg); opt_blocks = 1; break;
        case 'f': feature_mask_file_in  = optarg; break;
        case 'F': feature_mask_file_out = optarg; break;
        case 'j': opt_json  = type_for_name(optarg); break;
        case 'm': opt_model = optarg; break;
        case 'P': opt_preport = 1; break;
        case 'p': opt_percentage = atoi(optarg) ; break;
        case 'r': opt_seed = atoi(optarg);break; /* seed the random number generator */
        case 'R': opt_reduce = atoi(optarg); assert(opt_reduce>0); break;
        case 't': opt_train = type_for_name(optarg); break;
        case 'x': opt_omit = 1; break;
        case 'h': opt_help++; break;
        case 'n': opt_ngram_mode = atoi(optarg);break;
        case 'T':
            if(optarg[0]=='-'){
                for(int i=1;;i++){
                    const char *name = name_for_type(i);
                    if(name==0) exit(0);
                    printf("%d\t%s\n",i,name);
                }
            }
            if(alldigits(optarg)){
                const char *name = name_for_type(atoi(optarg));
                if(name==0) fprintf(stderr,"%s: invalid number\n",optarg);
                else printf("%s\n",name);
            } else {
                printf("%d\n",type_for_name(optarg));
            }
            exit(0);
        }
    }
    if (opt_help) usage();

    argc -= optind;
    argv += optind;

    if(block_size<1){
        fprintf(stderr,"Invalid block size\n");
        usage();
    }

    s = sceadan_open(opt_model, opt_class_file, feature_mask_file_in);
    sceadan_set_ngram_mode(s,opt_ngram_mode);

    if (opt_reduce!=0){
        // feature reducetion generate new feature_mask file 
        assert(feature_mask_file_out!=0);
        int ret = sceadan_reduce_feature(s, feature_mask_file_out, opt_reduce);
        exit(ret);
    }

    if(argc < 1) usage();
    if(strcmp(argv[0],"-")==0){         /* process stdin */
        process_file("-",0,FTW_F);      /* FTW_F is not correct, but it works with process_file */
    }

    while(argc>0){
        process_dir(*argv);
        argc--;
        argv++;
    }
    exit(0);
}
