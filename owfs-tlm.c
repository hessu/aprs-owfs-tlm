
/*
 *	owfs-tlm
 *
 *	(c) Heikki Hannikainen, OH7LZB <hessu@hes.iki.fi>
 *
 *	This program is licensed under the BSD license, which can be found
 *	in the file LICENSE.
 *	
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
                     
#define ME "owfs-tlm"
#define VERSION "1.0"
#define VERSTR ME " version " VERSION

#define HELPS VERSTR "help\n"

struct source_t {
	char *file;
	double value;
	int scaled;
};

struct source_t *sources[5];
int source_count = 0;

int fork_a_daemon = 0;

char *packet = NULL;
char *beaconfile = NULL;

void *hmalloc(size_t size)
{
	void *p = malloc(size);
	
	if (p == NULL) {
		fprintf(stderr, ME ": out of memory\n");
		abort();
	}
	
	return p;
}

char *hstrdup(const char *s)
{
	char *o = strdup(s);
        
        if (o == NULL) {
                fprintf(stderr, ME ": out of memory\n");
                abort();
        }
        
        return o;
}

/*
 *	Parse arguments
 */

void parse_cmdline(int argc, char *argv[])
{
	int s;
	int failed = 0;
	
	while ((s = getopt(argc, argv, "t:p:b:f?h")) != -1) {
	switch (s) {
        	case 't':
        		// temperature
        		sources[source_count] = hmalloc(sizeof(struct source_t));
        		sources[source_count]->file = hstrdup(optarg);
        		sources[source_count]->value = 0.0;
        		sources[source_count]->scaled = 0;
        		source_count++;
        		break;
                case 'p':
                        // packet
                        packet = hstrdup(optarg);
                        break;
                case 'b':
                        // beacon file
                        beaconfile = hstrdup(optarg);
                        break;
		case 'f':
			fork_a_daemon = 1;
			break;
		case '?':
		case 'h':
			fprintf(stderr, "%s", VERSTR);
			failed = 1;
	}
	}
	
	if (failed) {
		fputs(HELPS, stderr);
		exit(failed);
	}
}

/*
 *	signal handler
 */
 
int sighandler(int signum)
{
	switch (signum) {

	case SIGINT:
	case SIGTERM:
	case SIGQUIT:
		fprintf(stderr, ME " Shutting down on signal %d\n", signum);
		exit(0);
		
	default:
		fprintf(stderr, ME " SIG %d ignored\n", signum);
		break;
	}
	
	signal(signum, (void *)sighandler);	/* restore handler */
	return 0;
		
}

#define OWFS_BUFLEN	64

void process_owfs_buf(struct source_t *src, char *buf, int r)
{
        src->value = atof(buf);
        // values -55 to +125 (180 range) scaled to 0..8280
        double scaled = (src->value + 55.0) * (8280.0/180.0);
        printf("read %s: %.5f scaled %.0f\n", src->file, src->value, scaled);
        src->scaled = scaled;
}

void read_values(void)
{
        int i, f, r;
        char buf[OWFS_BUFLEN];
        struct source_t *src;
        
        for (i = 0; i < source_count; i++) {
                src = sources[i];
                f = open(src->file, O_RDONLY);
                src->value = 0.0;
                src->scaled = 0;
                if (f < 0) {
                        fprintf(stderr, ME " failed to open %s: %s\n", src->file, strerror(errno));
                        break;
                }
                
                r = read(f, buf, OWFS_BUFLEN-1);
                buf[r] = 0;
                buf[OWFS_BUFLEN-1] = 0;
                //printf("read %d\n", r);
                
                if (r > 4)
                        process_owfs_buf(src, buf, r);
                
                if (close(f)) {
                        fprintf(stderr, ME " failed to close %s: %s\n", src->file, strerror(errno));
                        break;
                }
        }
}

char *base91(char *p, int i)
{
        *p++ = (i/91) + 33;
        *p++ = i % 91 + 33;
        
        return p;
}

#define PBUFLEN 256

int tlm_seq = 0;

char *produce_tlm(void)
{
        int i;
        static char tbuf[PBUFLEN];
        char *p, *e;
        
        p = tbuf;
        e = p + PBUFLEN;
        *p = 0;
        
        *p++ = '|';
        
        p = base91(p, tlm_seq);
        
        for (i = 0; i < source_count; i++) {
                p = base91(p, sources[i]->scaled);
        }
        *p++ = '|';
        *p++ = 0;
        
        tlm_seq = (tlm_seq + 1) & 0x1FFF;
        
        return tbuf;
}

void produce_beacon(void)
{
        int f;
        char *tmpf;
        int n;
        
        char buf[PBUFLEN];
        n = snprintf(buf, PBUFLEN, "%s%s", packet, produce_tlm());
        printf("%s\n", buf);
        
        if (!beaconfile)
                return;
                
        tmpf = hmalloc(strlen(beaconfile) + 10);
        sprintf(tmpf, "%s.tmp", beaconfile);
        
        f = open(tmpf, O_WRONLY|O_CREAT|O_TRUNC);
        if (f < 0) {
                fprintf(stderr, ME " failed to open %s: %s\n", tmpf, strerror(errno));
                hfree(tmpf);
                return;
        }
        
        write(f, buf, n);
        
        if (close(f)) {
                fprintf(stderr, ME " failed to close %s: %s\n", tmpf, strerror(errno));
                hfree(tmpf);
                return;
        }
        
        if (rename(tmpf, beaconfile)) {
                fprintf(stderr, ME " failed to rename %s to %s: %s\n", tmpf, beaconfile, strerror(errno));
                hfree(tmpf);
                return;
        }
        
        hfree(tmpf);
}

int main(int argc, char **argv)
{
	/* command line */
	parse_cmdline(argc, argv);
	
	while (1) {
	        read_values();
	        produce_beacon();
	        sleep(10);
	}
	
	return 0;
}

