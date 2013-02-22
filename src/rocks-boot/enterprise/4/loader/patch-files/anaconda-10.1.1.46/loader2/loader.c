/*
 * loader.c
 *
 * This is the installer loader.  Its job is to somehow load the rest
 * of the installer into memory and run it.  This may require setting
 * up some devices and networking, etc. The main point of this code is
 * to stay SMALL! Remember that, live by that, and learn to like it.
 *
 * Erik Troan <ewt@redhat.com>
 * Matt Wilson <msw@redhat.com>
 * Michael Fulbright <msf@redhat.com>
 * Jeremy Katz <katzj@redhat.com>
 *
 * Copyright 1997 - 2004 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <newt.h>
#include <popt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <linux/fb.h>
#include <linux/serial.h>

#include "loader.h"
#include "loadermisc.h" /* JKFIXME: functions here should be split out */
#include "log.h"
#include "lang.h"
#include "kbd.h"
#include "kickstart.h"
#include "windows.h"

/* module stuff */
#include "modules.h"
#include "moduleinfo.h"
#include "moduledeps.h"
#include "modstubs.h"

#include "driverdisk.h"

/* hardware stuff */
#include "hardware.h"
#include "firewire.h"
#include "pcmcia.h"
#include "usb.h"

/* install method stuff */
#include "method.h"
#include "cdinstall.h"
#include "nfsinstall.h"
#include "hdinstall.h"
#include "urlinstall.h"

#include "net.h"
#include "telnetd.h"

#include "selinux.h"

#include "../isys/imount.h"
#include "../isys/isys.h"
#include "../isys/stubs.h"
#include "../isys/lang.h"
#include "../isys/eddsupport.h"

/* maximum number of extra arguments that can be passed to the second stage */
#define MAX_EXTRA_ARGS 128
static char * extraArgs[MAX_EXTRA_ARGS];
static int hasGraphicalOverride();

static int newtRunning = 0;


#ifdef INCLUDE_LOCAL
#include "cdinstall.h"
#include "hdinstall.h"
#endif
#ifdef INCLUDE_NETWORK
#include "nfsinstall.h"
#include "urlinstall.h"
#endif

int num_link_checks = 5;
int post_link_sleep = 0;

static struct installMethod installMethods[] = {
#if !defined(__s390__) && !defined(__s390x__)
    { N_("Local CDROM"), "cdrom", 0, CLASS_CDROM, mountCdromImage },
#endif
    { N_("Hard drive"), "hd", 0, CLASS_HD, mountHardDrive },
    { N_("NFS image"), "nfs", 1, CLASS_NETWORK, mountNfsImage },
    { "FTP", "ftp", 1, CLASS_NETWORK, mountUrlImage },
    { "HTTP", "http", 1, CLASS_NETWORK, mountUrlImage },
};
static int numMethods = sizeof(installMethods) / sizeof(struct installMethod);

/* JKFIXME: bad hack for second stage modules without module-info */
struct moduleBallLocation * secondStageModuleLocation;

#if 0
#if !defined(__s390__) && !defined(__s390x__)
#define RAMDISK_DEVICE "/dev/ram"
#else
#define RAMDISK_DEVICE "/dev/ram2"
#endif

int setupRamdisk(void) {
    gzFile f;
    static int done = 0;

    if (done) return 0;

    done = 1;

    f = gunzip_open("/etc/ramfs.img");
    if (f) {
        char buf[10240];
        int i, j = 0;
        int fd;
        
        fd = open(RAMDISK_DEVICE, O_RDWR);
        logMessage("copying file to fd %d", fd);
        
        while ((i = gunzip_read(f, buf, sizeof(buf))) > 0) {
            j += write(fd, buf, i);
        }
        
        logMessage("wrote %d bytes", j);
        close(fd);
        gunzip_close(f);
    }
    
    if (doPwMount(RAMDISK_DEVICE, "/tmp/ramfs", "ext2", 0, 0, NULL, NULL, 0, 0, NULL))
        logMessage("failed to mount ramfs image");
    
    return 0;
}
#endif

void setupRamfs(void) {
    mkdirChain("/tmp/ramfs");
    doPwMount("none", "/tmp/ramfs", "ramfs", 0, 0, NULL, NULL, 0, 0, NULL);
}


void doSuspend(void) {
    newtFinished();
    exit(1);
}

void startNewt(int flags) {
    if (!newtRunning) {
        char *buf = sdupprintf(_("Welcome to %s"), getProductName());
        newtInit();
        newtCls();
        newtDrawRootText(0, 0, buf);
        free(buf);
        
        newtPushHelpLine(_("  <Tab>/<Alt-Tab> between elements  | <Space> selects | <F12> next screen "));
        
        newtRunning = 1;
        if (FL_TESTING(flags)) 
            newtSetSuspendCallback((void *) doSuspend, NULL);
    }
}

void stopNewt(void) {
    if (newtRunning) newtFinished();
    newtRunning = 0;
}

static char * productName = NULL;
static char * productPath = NULL;

static void initProductInfo(void) {
    FILE *f;
    int i;

    f = fopen("/.buildstamp", "r");
    if (!f) {
        productName = strdup("anaconda");
	productPath = strdup("RedHat");
    } else {
	productName = malloc(256);
	productPath = malloc(256);
        fgets(productName, 256, f); /* stamp time */
        fgets(productName, 256, f); /* product name */
	fgets(productPath, 256, f); /* product version */
	fgets(productPath, 256, f); /* product path */

        i = strlen(productName) - 1;
	while (isspace(*(productName + i))) {
            *(productName + i) = '\0';
            i--;
        }
        i = strlen(productPath) - 1;
	while (isspace(*(productPath + i))) {
            *(productPath + i) = '\0';
            i--;
        }
    }
}

char * getProductName(void) {
    if (!productName) {
       initProductInfo();
    }
    return productName;
}

char * getProductPath(void) {
    if (!productPath) {
       initProductInfo();
    }
    return productPath;
}



	

void initializeConsole(moduleList modLoaded, moduleDeps modDeps,
                       moduleInfoSet modInfo, int flags) {
    if (!FL_NOFB(flags))
	mlLoadModuleSet("vgastate:vga16fb", modLoaded, modDeps, modInfo, flags);
    /* enable UTF-8 console */
    printf("\033%%G");
    fflush(stdout);
    isysLoadFont();
    if (!FL_TESTING(flags))
        isysSetUnicodeKeymap();
}

static void spawnShell(int flags) {
    pid_t pid;
    int fd;

    if (FL_SERIAL(flags) || FL_NOSHELL(flags)) {
        logMessage("not spawning a shell");
        return;
    }

    fd = open("/dev/tty2", O_RDWR);
    if (fd < 0) {
        logMessage("cannot open /dev/tty2 -- no shell will be provided");
        return;
    } else if (access("/bin/sh",  X_OK))  {
        logMessage("cannot open shell - /bin/sh doesn't exist");
        return;
    }
    
    if (!(pid = fork())) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        
        close(fd);
        setsid();

	/* enable UTF-8 console */
	printf("\033%%G");
	fflush(stdout);
	isysLoadFont();
	
        if (ioctl(0, TIOCSCTTY, NULL)) {
            logMessage("could not set new controlling tty");
        }
        
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        setenv("LD_LIBRARY_PATH", LIBPATH, 1);
        
        execl("/bin/sh", "-/bin/sh", NULL);
        logMessage("exec of /bin/sh failed: %s", strerror(errno));
        exit(1);
    }
    
    close(fd);

    return;
}

void loadUpdates(int flags) {
    int done = 0;
    int rc;
    char * device = NULL, ** devNames = NULL;
    char * buf;
    int num = 0;

    do { 
        rc = getRemovableDevices(&devNames);
        if (rc == 0) 
            return;

        /* we don't need to ask which to use if they only have one */
        if (rc == 1) {
            device = strdup(devNames[0]);
            free(devNames);
        } else {
            startNewt(flags);
            rc = newtWinMenu(_("Update Disk Source"),
                             _("You have multiple devices which could serve "
                               "as sources for an update disk.  Which would "
                               "you like to use?"), 40, 10, 10,
                             rc < 6 ? rc : 6, devNames,
                             &num, _("OK"), _("Back"), NULL);
            
            if (rc == 2) {
                free(devNames);
                return;
            }
            device = strdup(devNames[num]);
            free(devNames);
        }


        buf = sdupprintf(_("Insert your updates disk into /dev/%s and press "
                           "\"OK\" to continue."), device);
        rc = newtWinChoice(_("Updates Disk"), _("OK"), _("Cancel"), buf);
        if (rc == 2)
            return;

        logMessage("UPDATES device is %s", device);

        devMakeInode(device, "/tmp/upd.disk");
        if (doPwMount("/tmp/upd.disk", "/tmp/update-disk", "ext2", 1, 0, 
                      NULL, NULL, 0, 0, NULL) && 
            doPwMount("/tmp/upd.disk", "/tmp/update-disk", "iso9660", 1, 0,
                      NULL, NULL, 0, 0, NULL)) {
            newtWinMessage(_("Error"), _("OK"), 
                           _("Failed to mount updates disk"));
        } else {
            /* Copy everything to /tmp/updates so we can unmount the disk  */
            winStatus(40, 3, _("Updates"), _("Reading anaconda updates..."));
            if (!copyDirectory("/tmp/update-disk", "/tmp/updates")) done = 1;
            newtPopWindow();
            umount("/tmp/update-disk");
        }
    } while (!done);
    
    return;
}

#if !defined(__s390__) && !defined(__s390x__)
static void checkForHardDrives(int * flagsPtr) {
    int flags = (*flagsPtr);
    int i;
    struct device ** devices;

    devices = probeDevices(CLASS_HD, BUS_UNSPEC, PROBE_LOADED);
    if (devices)
        return;

    /* If they're using kickstart, assume they might know what they're doing.
     * Worst case is we fail later */
    if (FL_KICKSTART(flags)) {
        logMessage("no hard drives found, but in kickstart so continuing anyway");
        return;
    }
    
    startNewt(flags);
    i = newtWinChoice(_("Warning"), _("Yes"), _("No"),
                      _("No hard drives have been found.  You probably need "
                        "to manually choose device drivers for the "
                        "installation to succeed.  Would you like to "
                        "select drivers now?"));
    if (i != 2) (*flagsPtr) = (*flagsPtr) | LOADER_FLAGS_ISA;

    return;
}
#endif

static void writeVNCPasswordFile(char *pfile, char *password) {
    FILE *f;

    f = fopen(pfile, "w+");
    fprintf(f, "%s\n", password);
    fclose(f);
}

/* read information from /tmp/netinfo (written by linuxrc) */
static void readNetInfo(int flags, struct loaderData_s ** ld) {
   struct loaderData_s * loaderData = *ld;
   FILE *f;
   char *end;
   char buf[100], *vname, *vparm;

   f = fopen("/tmp/netinfo", "r");
   if (!f) {
       return;
   }
   vname = (char *)malloc(sizeof(char)*15);
   vparm = (char *)malloc(sizeof(char)*85);

   while(fgets(buf, 100, f)) {
       if ((vname = strtok(buf, "="))) {
           vparm = strtok(NULL, "=");
           while (isspace(*vparm))
               vparm++;
           end = strchr(vparm, '\0');
           while (isspace(*end))
               end--;
           end++;
           *end = '\0';
           if (strstr(vname, "IPADDR")) {
               loaderData->ip = strdup(vparm);
               loaderData->ipinfo_set = 1;
           }
           if (strstr(vname, "NETMASK")) {
               loaderData->netmask = strdup(vparm);
           }
           if (strstr(vname, "GATEWAY")) {
               loaderData->gateway = strdup(vparm);
           }
           if (strstr(vname, "DNS")) {
               loaderData->dns = strdup(vparm);
           }
           if (strstr(vname, "MTU")) {
               loaderData->mtu = atoi(vparm);
           }
           if (strstr(vname, "PEERID")) {
               loaderData->peerid = strdup(vparm);
           }
           if (strstr(vname, "SUBCHANNELS")) {
               loaderData->subchannels = strdup(vparm);
           }
           if (strstr(vname, "PORTNAME")) {
               loaderData->portname = strdup(vparm);
           }
           if (strstr(vname, "NETTYPE")) {
               loaderData->nettype = strdup(vparm);
           }
           if (strstr(vname, "CTCPROT")) {
               loaderData->ctcprot = strdup(vparm);
           }
       }
   }
   fclose(f);
}

/* parse anaconda or pxelinux-style ip= arguments
 * pxelinux format: ip=<client-ip>:<boot-server-ip>:<gw-ip>:<netmask>
 * anaconda format: ip=<client-ip> netmask=<netmask> gateway=<gw-ip>
*/
static void parseCmdLineIp(struct loaderData_s * loaderData, char *argv)
{
  /* Detect pxelinux */
  if (strstr(argv, ":") != NULL) {
    char *start, *end;

    /* IP */
    start = argv + 3;
    end = strstr(start, ":");
    loaderData->ip = strndup(start, end-start);
    loaderData->ipinfo_set = 1;

    /* Boot server */
    if (end + 1 == '\0')
      return;
    start = end + 1;
    end = strstr(start, ":");
    if (end == NULL)
      return;

    /* Gateway */
    if (end + 1 == '\0')
      return;
    start = end + 1;
    end = strstr(start, ":");
    if (end == NULL) {
      loaderData->gateway = strdup (start);
      return;
    } else 
      loaderData->gateway = strndup(start, end-start);

    /* Netmask */
    if (end + 1 == '\0')
      return;
    start = end + 1;
    loaderData->netmask = strdup(start);
  } else {
    loaderData->ip = strdup(argv + 3);
    loaderData->ipinfo_set = 1;
  }
}

/* parses /proc/cmdline for any arguments which are important to us.  
 * NOTE: in test mode, can specify a cmdline with --cmdline
 */
static int parseCmdLineFlags(int flags, struct loaderData_s * loaderData,
                             char * cmdLine) {
    int fd;
    char buf[1024];
    int len;
    char ** argv;
    int argc;
    int numExtraArgs = 0;
    int i;

    /* if we have any explicit cmdline (probably test mode), we don't want
     * to parse /proc/cmdline */
    if (!cmdLine) {
        if ((fd = open("/proc/cmdline", O_RDONLY)) < 0) return flags;
        len = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (len <= 0) return flags;
        
        buf[len] = '\0';
        cmdLine = buf;
    }
    
    if (poptParseArgvString(cmdLine, &argc, (const char ***) &argv))
        return flags;

    /* we want to default to graphical and allow override with 'text' */
    flags |= LOADER_FLAGS_GRAPHICAL;

    for (i=0; i < argc; i++) {
        if (!strcasecmp(argv[i], "expert")) {
            flags |= LOADER_FLAGS_EXPERT;
            logMessage("expert got used, ignoring");
            /*            flags |= (LOADER_FLAGS_EXPERT | LOADER_FLAGS_MODDISK | 
                          LOADER_FLAGS_ASKMETHOD);*/
        } else if (!strcasecmp(argv[i], "askmethod"))
            flags |= LOADER_FLAGS_ASKMETHOD;
        else if (!strcasecmp(argv[i], "noshell"))
            flags |= LOADER_FLAGS_NOSHELL;
        else if (!strcasecmp(argv[i], "mediacheck"))
            flags |= LOADER_FLAGS_MEDIACHECK;
        else if (!strcasecmp(argv[i], "nousbstorage"))
            flags |= LOADER_FLAGS_NOUSBSTORAGE;
        else if (!strcasecmp(argv[i], "nousb"))
            flags |= LOADER_FLAGS_NOUSB;
        else if (!strcasecmp(argv[i], "telnet"))
            flags |= LOADER_FLAGS_TELNETD;
        else if (!strcasecmp(argv[i], "nofirewire"))
            flags |= LOADER_FLAGS_NOIEEE1394;
        else if (!strcasecmp(argv[i], "nonet"))
            flags |= LOADER_FLAGS_NONET;
        else if (!strcasecmp(argv[i], "nostorage"))
            flags |= LOADER_FLAGS_NOSTORAGE;
        else if (!strcasecmp(argv[i], "noprobe"))
            flags |= (LOADER_FLAGS_NONET | LOADER_FLAGS_NOSTORAGE | LOADER_FLAGS_NOUSB | LOADER_FLAGS_NOIEEE1394);
        else if (!strcasecmp(argv[i], "nopcmcia"))
            flags |= LOADER_FLAGS_NOPCMCIA;
        else if (!strcasecmp(argv[i], "noparport"))
            flags |= LOADER_FLAGS_NOPARPORT;
        else if (!strcasecmp(argv[i], "text")) {
            flags |= LOADER_FLAGS_TEXT;
	    flags &= ~LOADER_FLAGS_GRAPHICAL;
	} else if (!strcasecmp(argv[i], "graphical"))
            flags |= LOADER_FLAGS_GRAPHICAL;
        else if (!strcasecmp(argv[i], "cmdline"))
            flags |= LOADER_FLAGS_CMDLINE;
        else if (!strcasecmp(argv[i], "updates"))
            flags |= LOADER_FLAGS_UPDATES;
        else if (!strcasecmp(argv[i], "isa"))
            flags |= LOADER_FLAGS_ISA;
        else if (!strncasecmp(argv[i], "dd=", 3) || 
                 !strncasecmp(argv[i], "driverdisk=", 11)) {
            loaderData->ddsrc = strdup(argv[i] + 
                                       (argv[i][1] == 'r' ? 11 : 3));
        } else if (!strcasecmp(argv[i], "dd") || 
                   !strcasecmp(argv[i], "driverdisk"))
            flags |= LOADER_FLAGS_MODDISK;
        else if (!strcasecmp(argv[i], "rescue"))
            flags |= LOADER_FLAGS_RESCUE;
        else if (!strcasecmp(argv[i], "nopass"))
            flags |= LOADER_FLAGS_NOPASS;
        else if (!strcasecmp(argv[i], "serial")) 
            flags |= LOADER_FLAGS_SERIAL;
        else if (!strcasecmp(argv[i], "nofb"))
            flags |= LOADER_FLAGS_NOFB;
        else if (!strcasecmp(argv[i], "kssendmac"))
            flags |= LOADER_FLAGS_KICKSTART_SEND_MAC;
        else if (!strncasecmp(argv[i], "debug=", 6))
                  setLogLevel(strtol(argv[i] + 6, (char **)NULL, 10));
        else if (!strncasecmp(argv[i], "ksdevice=", 9)) {
            loaderData->netDev = strdup(argv[i] + 9);
            loaderData->netDev_set = 1;
	} else if (!strncmp(argv[i], "BOOTIF=", 7)) {
	    /* +10 so that we skip over the leading 01- */
            loaderData->bootIf = strdup(argv[i] + 10);
            loaderData->bootIf_set = 1;
        } else if (!strncasecmp(argv[i], "dhcpclass=", 10)) {
            loaderData->netCls = strdup(argv[i] + 10);
            loaderData->netCls_set = 1;
        }
        else if (!strcasecmp(argv[i], "ks") || !strncasecmp(argv[i], "ks=", 3))
            loaderData->ksFile = strdup(argv[i]);
#ifdef SDSC
	else if (!strncasecmp(argv[i], "dist=", 5)) {
		loaderData->distName = strdup(argv[i]+5);
	}
	else if (!strncasecmp(argv[i], "dropcert", 8)) {
		loaderData->dropCert = 1;
	}
	else if (!strncasecmp(argv[i], "ekv", 3)) {
		loaderData->ekv = 1; 
	}
	else if (!strncasecmp(argv[i], "mac=", 4)) {
		loaderData->mac = strdup(argv[i]+4);
	}
	else if (!strncasecmp(argv[i], "central=", 8)) {
		char    *p;
		char	*cgi = "install/sbin/wan/kickstart.cgi";

		p = (char*) malloc(strlen(argv[i]) + strlen(cgi) + 12);

		sprintf(p, "ks=http://%s/%s", argv[i]+8, cgi);
		loaderData->ksFile = p;
		logMessage("ROCKS:central:server is %s", loaderData->ksFile);
	}
#endif
        else if (!strncasecmp(argv[i], "display=", 8))
            setenv("DISPLAY", argv[i] + 8, 1);
        else if ((!strncasecmp(argv[i], "lang=", 5)) && 
                 (strlen(argv[i]) > 5))  {
            loaderData->lang = strdup(argv[i] + 5);
            loaderData->lang_set = 1;
        } else if (!strncasecmp(argv[i], "keymap=", 7) &&
                   (strlen(argv[i]) > 7)) {
            loaderData->kbd = strdup(argv[i] + 7);
            loaderData->kbd_set = 1;
        } else if (!strncasecmp(argv[i], "method=", 7)) {
            setMethodFromCmdline(argv[i] + 7, loaderData);
        } else if (!strncasecmp(argv[i], "ip=", 3)) {
            parseCmdLineIp(loaderData, argv[i]);
        } else if (!strncasecmp(argv[i], "mtu=", 4)) 
            loaderData->mtu = atoi(argv[i] + 4);
        else if (!strncasecmp(argv[i], "netmask=", 8)) 
            loaderData->netmask = strdup(argv[i] + 8);
        else if (!strncasecmp(argv[i], "gateway=", 8))
            loaderData->gateway = strdup(argv[i] + 8);
        else if (!strncasecmp(argv[i], "dns=", 4))
            loaderData->dns = strdup(argv[i] + 4);
        else if (!strncasecmp(argv[i], "ethtool=", 8))
            loaderData->ethtool = strdup(argv[i] + 8);
        else if (!strncasecmp(argv[i], "essid=", 6))
            loaderData->essid = strdup(argv[i] + 6);
        else if (!strncasecmp(argv[i], "wepkey=", 7))
            loaderData->wepkey = strdup(argv[i] + 7);
        else if (!strncasecmp(argv[i], "linksleep=", 10))
            num_link_checks = atoi(argv[i] + 10);
        else if (!strncasecmp(argv[i], "nicdelay=", 9))
            post_link_sleep = atoi(argv[i] + 9);
        else if (!strncasecmp(argv[i], "selinux=0", 9))
            flags &= ~LOADER_FLAGS_SELINUX;
        else if (!strncasecmp(argv[i], "selinux", 7))
            flags |= LOADER_FLAGS_SELINUX;
        else if (!strncasecmp(argv[i], "nfsmountopts=", 13))
            loaderData->nfsmountopts = strdup(argv[i] + 13);
        else if (numExtraArgs < (MAX_EXTRA_ARGS - 1)) {
            /* go through and append args we just want to pass on to */
            /* the anaconda script, but don't want to represent as a */
            /* LOADER_FLAGS_XXX since loader doesn't care about these */
            /* particular options.                                   */
	    /* do vncpassword case first */
            if (!strncasecmp(argv[i], "vncpassword=", 12)) {
		if (!FL_TESTING(flags))
		    writeVNCPasswordFile("/tmp/vncpassword.dat", argv[i]+12);
	    } else if (!strncasecmp(argv[i], "resolution=", 11) ||
                !strncasecmp(argv[i], "lowres", 6) ||
                !strncasecmp(argv[i], "skipddc", 7) ||
                !strncasecmp(argv[i], "nomount", 7) ||
                !strncasecmp(argv[i], "vnc", 3) ||
		!strncasecmp(argv[i], "vncconnect=", 11) ||
                !strncasecmp(argv[i], "headless", 8)) {
                int arglen;

		/* vnc implies graphical */
		if (!strncasecmp(argv[i], "vnc", 3))
		    flags |= LOADER_FLAGS_GRAPHICAL;

                arglen = strlen(argv[i])+3;
                extraArgs[numExtraArgs] = (char *) malloc(arglen*sizeof(char));
                snprintf(extraArgs[numExtraArgs], arglen, "--%s", argv[i]);
                numExtraArgs = numExtraArgs + 1;
        
                if (numExtraArgs > (MAX_EXTRA_ARGS - 2)) {
                    logMessage("Too many command line arguments (max allowed is %d), "
                               "rest will be dropped.", MAX_EXTRA_ARGS);
                }
	    }
	}
    }

#ifdef SDSC
	/*
	 * if 'ksdevice=' isn't specified on the command line, then
	 * default the kickstart device to eth1
	 *
	 * on frontend installs, this will bypass an anaconda screen that
	 * asks the user to choose which network interface should be
	 * used for the installation.
	 * 
	 * if the user wishes to use a different interface, they just need
	 * to specify 'ksdevice=ethx' where 'ethx' is their interface of
	 * choice.
	 */
	if (loaderData->netDev_set != 1) {
		loaderData->netDev = strdup("eth1");
		loaderData->netDev_set = 1;
	}
#endif

    readNetInfo(flags, &loaderData);

    /* NULL terminates the array of extra args */
    extraArgs[numExtraArgs] = NULL;

    return flags;
}


#if 0
/* determine if we are using a framebuffer console.  return 1 if so */
static int checkFrameBuffer() {
    int fd;
    int rc = 0;
    struct fb_fix_screeninfo fix;

    if ((fd = open("/dev/fb0", O_RDONLY)) == -1) {
        return 0;
    }
    
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) >= 0) {
        rc = 1;
    }
    close(fd);
    return rc;
}
#endif


/* make sure they have enough ram */
static void checkForRam(int flags) {
    if (totalMemory() < MIN_RAM) {
        char *buf;
        buf = sdupprintf(_("You do not have enough RAM to install %s "
                           "on this machine."), getProductName());
        startNewt(flags);
        newtWinMessage(_("Error"), _("OK"), buf);
        free(buf);
        stopNewt();
        exit(0);
    }
}

static int haveDeviceOfType(int type, moduleList modLoaded) {
    struct device ** devices;

    devices = probeDevices(type, BUS_UNSPEC, PROBE_LOADED);
    if (devices) {
        return 1;
#if 0
        int i;
        for (i = 0; devices[i]; i++) {
            if (devices[i]->driver && mlModuleInList(devices[i]->driver,
                                                     modLoaded)) {
                logMessage("devices[%d] is %s - %s using %s (loaded)", i, devices[i]->desc, devices[i]->device, devices[i]->driver);
                return 1;
            } else if (!devices[i]->driver) {
                logMessage("devices[%d] is %s - %s using %s (no driver)", i, devices[i]->desc, devices[i]->device, devices[i]->driver);
                return 1;
            } else {
                logMessage("devices[%d] is %s - %s using %s (not loaded)", i, devices[i]->desc, devices[i]->device, devices[i]->driver);
            }
        }
#endif
    }
    return 0;
}

/* fsm for the basics of the loader. */
static char *doLoaderMain(char * location,
                          struct loaderData_s * loaderData,
                          moduleInfoSet modInfo,
                          moduleList modLoaded,
                          moduleDeps * modDepsPtr,
                          int flags) {
    enum { STEP_LANG, STEP_KBD, STEP_METHOD, STEP_DRIVER, 
           STEP_DRIVERDISK, STEP_NETWORK, STEP_IFACE,
           STEP_IP, STEP_URL, STEP_DONE } step;
    char * url = NULL;
    int dir = 1;
    int rc, i;

    char * installNames[10]; /* 10 install methods will be enough for anyone */
    int numValidMethods = 0;
    int validMethods[10];
    int methodNum = -1;

    int needed = -1;
    int needsNetwork = 0;

    int rhcdfnd = 0;

    char * devName = NULL;
    static struct networkDeviceConfig netDev;

    char * kbdtype = NULL;

    for (i = 0; i < numMethods; i++, numValidMethods++) {
        installNames[numValidMethods] = installMethods[i].name;
        validMethods[numValidMethods] = i;

        /* have we preselected this to be our install method? */
        if (loaderData->method && *loaderData->method && 
            !strcmp(loaderData->method, installMethods[i].shortname)) {
            methodNum = numValidMethods;
            /* disable the fast path (#102652) */
            flags |= LOADER_FLAGS_ASKMETHOD;
        }
    }

    installNames[numValidMethods] = NULL;

    /* check to see if we have a Red Hat Linux CD.  If we have one, then
     * we can fast-path the CD and not make people answer questions in 
     * text mode.  */
    if (!FL_ASKMETHOD(flags) && !FL_KICKSTART(flags)) {
        url = findRedHatCD(location, modInfo, modLoaded, * modDepsPtr, flags, !FL_RESCUE(flags));
	/* if we found a CD and we're not in rescue or vnc mode return */
	/* so we can short circuit straight to stage 2 from CD         */
	if (url && (!FL_RESCUE(flags) && !hasGraphicalOverride()))
	    return url;
	else {
	    rhcdfnd = 1;
            methodNum = 0; /* FIXME: this assumes cdrom is always first */
        }
    }

    if (!FL_CMDLINE(flags))
        startNewt(flags);

    step = STEP_LANG;

    while (step != STEP_DONE) {
        switch(step) {
        case STEP_LANG:
            if (loaderData->lang && (loaderData->lang_set == 1)) {
                setLanguage(loaderData->lang, flags);
            } else {
                chooseLanguage(&loaderData->lang, flags);
            }
            step = STEP_KBD;
            dir = 1;
            break;
        case STEP_KBD:
            if (loaderData->kbd && (loaderData->kbd_set == 1)) {
                /* JKFIXME: this is broken -- we should tell of the 
                 * failure; best by pulling code out in kbd.c to use */
                if (isysLoadKeymap(loaderData->kbd)) {
                    logMessage("requested keymap %s is not valid, asking", loaderData->kbd);
                    loaderData->kbd = NULL;
                    loaderData->kbd_set = 0;
                    break;
                }
                rc = LOADER_NOOP;
            } else {
                /* JKFIXME: should handle kbdtype, too probably... but it 
                 * just matters for sparc */
                rc = chooseKeyboard(&loaderData->kbd, &kbdtype, flags);
            }
            if (rc == LOADER_NOOP) {
                if (dir == -1)
                    step = STEP_LANG;
                else
                    step = STEP_METHOD;
                break;
            }

            if (rc == LOADER_BACK) {
                step = STEP_LANG;
                dir = -1;
            } else {
                step = STEP_METHOD;
                dir = 1;
            }

            break;

        case STEP_METHOD:
            /* this is kind of crappy, but we want the first few questions
             * to be asked when using rescue mode even if we're going
             * to short-circuit to the CD.
             *
             * Alternately, if we're in a VNC install based from CD we
             * can skip this step because we already found the CD */
            if (url) {
		if (FL_RESCUE(flags)) {
		    return url;
	        } else if (rhcdfnd) {
		    step = STEP_NETWORK;
		    dir = 1;
		    break;
		}
	    }	    

            needed = -1;

            if (loaderData->method && (methodNum != -1)) {
                rc = 1;
            } else {
                /* we need to set these each time through so that we get
                 * updated for language changes (#83672) */
                for (i = 0; i < numMethods; i++) {
                    installNames[i] = _(installMethods[i].name);
                }
                installNames[i] = NULL;

                rc = newtWinMenu(FL_RESCUE(flags) ? _("Rescue Method") :
                                 _("Installation Method"),
                                 FL_RESCUE(flags) ?
                                 _("What type of media contains the rescue "
                                   "image?") :
                                 _("What type of media contains the packages to "
                                   "be installed?"),
                                 30, 10, 20, 6, installNames, &methodNum, 
                                 _("OK"), _("Back"), NULL);
            } 
            if (rc && rc != 1) {
                step = STEP_KBD;
                dir = -1;
            } else {
                needed = installMethods[validMethods[methodNum]].deviceType;
                step = STEP_DRIVER;
                dir = 1;
            }
            break;

        case STEP_DRIVER: {
            if (needed == -1 || haveDeviceOfType(needed, modLoaded)) {
                step = STEP_NETWORK;
                dir = 1;
                needed = -1;
                break;
            }


            rc = newtWinTernary(_("No driver found"), _("Select driver"),
                                _("Use a driver disk"), _("Back"),
                                _("Unable to find any devices of the type "
                                  "needed for this installation type.  "
                                  "Would you like to manually select your "
                                  "driver or use a driver disk?"));
            if (rc == 2) {
                step = STEP_DRIVERDISK;
                dir = 1;
                break;
            } else if (rc == 3) {
                step = STEP_METHOD;
                dir = -1;
                break;
            }
            
            chooseManualDriver(installMethods[validMethods[methodNum]].deviceType,
                                    modLoaded, modDepsPtr, modInfo, flags);
            /* it doesn't really matter what we return here; we just want
             * to reprobe and make sure we have the driver */
            step = STEP_DRIVER;
            break;
        }

        case STEP_DRIVERDISK:

            rc = loadDriverFromMedia(needed,
                                     modLoaded, modDepsPtr, modInfo, 
                                     flags, 0, 0);
            if (rc == LOADER_BACK) {
                step = STEP_DRIVER;
                dir = -1;
                break;
            }

            /* need to come back to driver so that we can ensure that we found
             * the right kind of driver after loading the driver disk */
            step = STEP_DRIVER;
            break;

        case STEP_NETWORK:
#ifdef SDSC
		/* Dont ask for network information twice! */
		if (loaderData->ipinfo_set) {
			/* Need loopback up for cd install. */
			initLoopback();

			if (dir == 1)
				step = STEP_URL;
			else if (dir == -1)
				step = STEP_METHOD;
			break;
		}
#endif
            if ( (installMethods[validMethods[methodNum]].deviceType != 
                  CLASS_NETWORK) && (!hasGraphicalOverride())) {
                needsNetwork = 0;
                if (dir == 1) 
                    step = STEP_URL;
                else if (dir == -1)
                    step = STEP_METHOD;
                break;
            }

            needsNetwork = 1;
            if (!haveDeviceOfType(CLASS_NETWORK, modLoaded)) {
                needed = CLASS_NETWORK;
                step = STEP_DRIVER;
                break;
            }
            logMessage("need to set up networking");

            initLoopback();
            memset(&netDev, 0, sizeof(netDev));
            netDev.isDynamic = 1;
            
            /* fall through to interface selection */
        case STEP_IFACE:
            logMessage("going to pick interface");
            rc = chooseNetworkInterface(loaderData, flags);
            if ((rc == LOADER_BACK) || (rc == LOADER_ERROR) ||
                ((dir == -1) && (rc == LOADER_NOOP))) {
                step = STEP_METHOD;
                dir = -1;
                break;
            }

            devName = loaderData->netDev;
            strcpy(netDev.dev.device, devName);

            /* fall through to ip config */
        case STEP_IP:
            if (!needsNetwork) {
                step = STEP_METHOD; /* only hit going back */
                break;
            }

            logMessage("going to do getNetConfig");
	    /* populate netDev based on any kickstart data */
	    setupNetworkDeviceConfig(&netDev, loaderData, flags);

            rc = readNetConfig(devName, &netDev, loaderData->netCls, flags);
            if ((rc == LOADER_BACK) || (rc == LOADER_ERROR) ||
                ((dir == -1) && (rc == LOADER_NOOP))) {
                step = STEP_IFACE;
                dir = -1;
                break;
            }

            writeNetInfo("/tmp/netinfo", &netDev);
            step = STEP_URL;
            dir = 1;
            
        case STEP_URL:
            logMessage("starting to STEP_URL");
	    /* if we found a CD already short circuit out */
	    /* we get this case when we're doing a VNC install from CD */
	    /* and we didnt short circuit earlier because we had to */
	    /* prompt for network info for vnc to work */
            if (url && rhcdfnd)
		return url;

            url = installMethods[validMethods[methodNum]].mountImage(
                                      installMethods + validMethods[methodNum],
                                      location, loaderData, modInfo, modLoaded, 
                                      modDepsPtr, flags);
            if (!url) {
                step = STEP_IP ;
                dir = -1;
            } else {
                logMessage("got url %s", url);
                step = STEP_DONE;
                dir = 1;
            }
            break;
            
            
        default:
            break;
        }
    }

    return url;
}

static int manualDeviceCheck(moduleInfoSet modInfo, moduleList modLoaded,
                             moduleDeps * modDepsPtr, int flags) {
    char ** devices;
    int i, j, rc, num = 0;
    struct moduleInfo * mi;
    int width = 40;
    char * buf;

    do {
        devices = malloc((modLoaded->numModules + 1) * sizeof(*devices));
        for (i = 0, j = 0; i < modLoaded->numModules; i++) {
            if (!modLoaded->mods[i].weLoaded) continue;
            
            if (!(mi = findModuleInfo(modInfo, modLoaded->mods[i].name)) ||
                (!mi->description))
                continue;

            devices[j] = sdupprintf("%s (%s)", mi->description, 
                                    modLoaded->mods[i].name);
            if (strlen(devices[j]) > width)
                width = strlen(devices[j]);
            j++;
        }

        devices[j] = NULL;

        if (width > 70)
            width = 70;

        if (j > 0) {
            buf = _("The following devices have been found on your system.");
        } else {
            buf = _("No device drivers have been loaded for your system.  "
                    "Would you like to load any now?");
        }

        rc = newtWinMenu(_("Devices"), buf, width, 10, 20, 
                         (j > 6) ? 6 : j, devices, &num, _("Done"), 
                         _("Add Device"), NULL);

        /* no leaky */
        for (i = 0; i < j; i++) 
            free(devices[j]);
        free(devices);

        if (rc != 2)
            break;

        chooseManualDriver(CLASS_UNSPEC, modLoaded, modDepsPtr, modInfo, 
                           flags);
    } while (1);
    return 0;
}

/* JKFIXME: I don't really like this, but at least it isolates the ifdefs */
/* Either move dirname to %s_old or unlink depending on arch (unlink on all
 * !s390{,x} arches).  symlink to /mnt/runtime/dirname.  dirname *MUST* start
 * with a '/' */
static void migrate_runtime_directory(char * dirname) {
    char * runtimedir;

    runtimedir = sdupprintf("/mnt/runtime%s", dirname);
    if (!access(runtimedir, X_OK)) {
#if !defined(__s390__) && !defined(__s390x__)
        unlink(dirname);
#else
        char * olddir;

        olddir = sdupprintf("%s_old", dirname);
        rename(dirname, olddir);
        free(olddir);
#endif
        symlink(runtimedir, dirname);
    }
    free(runtimedir);
}


static int hasGraphicalOverride() {
    int i;

    if (getenv("DISPLAY"))
        return 1;

    for (i = 0; extraArgs[i] != NULL; i++) {
        if (!strncasecmp(extraArgs[i], "--vnc", 5))
            return 1;
    }
    return 0;
}

#ifdef SDSC
/*
 * Watchdog timer code
 */

#define WATCHDOG_QUANTUM	30	/* seconds */
#define WATCHDOG_TRIGGER	4	/* 120 seconds */


static int watchdog_ticks   = 0;
static int watchdog_enabled = 0;

static void
watchdog_handler(int sig)
{
	if ( watchdog_enabled ) {
		if ( ++watchdog_ticks > WATCHDOG_TRIGGER ) {
			exit(0);	/* reboot the machine */
		} else {
			alarm(WATCHDOG_QUANTUM);		
		}
	}
} /* watchdog_handler */

void
watchdog_reset()
{
	watchdog_ticks = 0;
}

void
watchdog_off()
{
	watchdog_enabled = 0;
	watchdog_ticks   = 0;

	/*
	 * cancel any pending alarm
	 */
	alarm(0);
} /* watchdog_off */


static void
watchdog_on()
{
	struct sigaction	sig;

	sig.sa_handler = watchdog_handler;
	sig.sa_flags   = SA_RESTART;
	sigemptyset(&sig.sa_mask);

	if ( sigaction(SIGALRM, &sig, NULL) < 0 ) {
		fprintf(stderr, "main:sigaction:failed:SIGARLM\n");
	}
	watchdog_ticks   = 0;
	watchdog_enabled = 1;
	alarm(WATCHDOG_QUANTUM);		
} /* watchdog_on */
#endif

int main(int argc, char ** argv) {
    int flags = LOADER_FLAGS_SELINUX;
    struct stat sb;
    struct serial_struct si;
    int rc, i;
    char * arg;
    FILE *f;

    char twelve = 12;

    moduleInfoSet modInfo;
    moduleList modLoaded;
    moduleDeps modDeps;

    char *url = NULL;

    char ** argptr, ** tmparg;
    char * anacondaArgs[50];
    int useRHupdates = 0;

    struct loaderData_s loaderData;

    char * cmdLine = NULL;
    char * ksFile = NULL;
    int testing = 0;
    int mediacheck = 0;
    char * virtpcon = NULL;
    poptContext optCon;
    struct poptOption optionTable[] = {
	{ "cmdline", '\0', POPT_ARG_STRING, &cmdLine, 0 },
        { "ksfile", '\0', POPT_ARG_STRING, &ksFile, 0 },
        { "test", '\0', POPT_ARG_NONE, &testing, 0 },
        { "mediacheck", '\0', POPT_ARG_NONE, &mediacheck, 0},
        { "virtpconsole", '\0', POPT_ARG_STRING, &virtpcon, 0 },
        { 0, 0, 0, 0, 0 }
    };

    /* JKFIXME: very very bad hack */
    secondStageModuleLocation = malloc(sizeof(struct moduleBallLocation));
    secondStageModuleLocation->path = strdup("/mnt/runtime/modules/modules.cgz");
    secondStageModuleLocation->version = CURRENT_MODBALLVER;
    
    if (!strcmp(argv[0] + strlen(argv[0]) - 6, "insmod"))
        return ourInsmodCommand(argc, argv);
    if (!strcmp(argv[0] + strlen(argv[0]) - 8, "modprobe"))
        return ourInsmodCommand(argc, argv);
    if (!strcmp(argv[0] + strlen(argv[0]) - 5, "rmmod"))
        return ourRmmodCommand(argc, argv);

    /* now we parse command line options */
    optCon = poptGetContext(NULL, argc, (const char **) argv, optionTable, 0);

    if ((rc = poptGetNextOpt(optCon)) < -1) {
        fprintf(stderr, "bad option %s: %s\n",
                poptBadOption(optCon, POPT_BADOPTION_NOALIAS), 
                poptStrerror(rc));
        exit(1);
    }

    if ((arg = (char *) poptGetArg(optCon))) {
        fprintf(stderr, "unexpected argument: %s\n", arg);
        exit(1);
    }

    if (!testing && !access("/var/run/loader.run", R_OK)) {
        printf(_("loader has already been run.  Starting shell.\n"));
        execl("/bin/sh", "-/bin/sh", NULL);
        exit(0);
    }
    
    f = fopen("/var/run/loader.run", "w+");
    fprintf(f, "%d\n", getpid());
    fclose(f);

    /* The fstat checks disallows serial console if we're running through
       a pty. This is handy for Japanese. */
    fstat(0, &sb);
    if (major(sb.st_rdev) != 3 && major(sb.st_rdev) != 136 && 
        (virtpcon == NULL)){
        if ((ioctl (0, TIOCLINUX, &twelve) < 0) && 
            (ioctl(0, TIOCGSERIAL, &si) != -1))
            flags |= LOADER_FLAGS_SERIAL;
    }

    if (testing) flags |= LOADER_FLAGS_TESTING;
    if (mediacheck) flags |= LOADER_FLAGS_MEDIACHECK;
    if (ksFile) flags |= LOADER_FLAGS_KICKSTART;
    if (virtpcon) flags |= LOADER_FLAGS_VIRTPCONSOLE;

    /* uncomment to send mac address in ks=http:/ header by default*/
    flags |= LOADER_FLAGS_KICKSTART_SEND_MAC;

    /* JKFIXME: I do NOT like this... it also looks kind of bogus */
#if defined(__s390__) && !defined(__s390x__)
    flags |= LOADER_FLAGS_NOSHELL | LOADER_FLAGS_NOUSB;
#endif

    openLog(FL_TESTING(flags));
    if (!FL_TESTING(flags))
        openlog("loader", 0, LOG_LOCAL0);

    memset(&loaderData, 0, sizeof(loaderData));


    extraArgs[0] = NULL;
    flags = parseCmdLineFlags(flags, &loaderData, cmdLine);

    if ((FL_SERIAL(flags) || FL_VIRTPCONSOLE(flags)) && 
        !hasGraphicalOverride())
        flags |= LOADER_FLAGS_TEXT;
    if (FL_SERIAL(flags))
        flags |= LOADER_FLAGS_NOFB;

    setupRamfs();

    arg = FL_TESTING(flags) ? "./module-info" : "/modules/module-info";
    modInfo = newModuleInfoSet();
    if (readModuleInfo(arg, modInfo, NULL, 0)) {
        fprintf(stderr, "failed to read %s\n", arg);
        sleep(5);
        exit(1);
    }
    mlReadLoadedList(&modLoaded);
    modDeps = mlNewDeps();
    mlLoadDeps(&modDeps, "/modules/modules.dep");

    initializeConsole(modLoaded, modDeps, modInfo, flags);
    checkForRam(flags);

    /* iSeries vio console users will be telnetting in to the primary
       partition, so use a terminal type that is appripriate */
    if (isVioConsole())
	setenv("TERM", "vt100", 1);
    
    mlLoadModuleSet("cramfs:vfat:nfs:loop:isofs:floppy:edd", 
                    modLoaded, modDeps, modInfo, flags);

    /* now let's do some initial hardware-type setup */
    lapicSetup(modLoaded, modDeps, modInfo, flags);
    ideSetup(modLoaded, modDeps, modInfo, flags);
    scsiSetup(modLoaded, modDeps, modInfo, flags);
    dasdSetup(modLoaded, modDeps, modInfo, flags);

    /* Note we *always* do this. If you could avoid this you could get
       a system w/o USB keyboard support, which would be bad. */
    usbInitialize(modLoaded, modDeps, modInfo, flags);
    
    /* now let's initialize any possible firewire.  fun */
    firewireInitialize(modLoaded, modDeps, modInfo, flags);

    /* explicitly read this to let libkudzu know we want to merge
     * in future tables rather than replace the initial one */
    pciReadDrivers("/modules/pcitable");
    
    if (loaderData.lang && (loaderData.lang_set == 1)) {
        setLanguage(loaderData.lang, flags);
    }

    if (!canProbeDevices() || FL_MODDISK(flags)) {
        startNewt(flags);
        
        loadDriverDisks(CLASS_UNSPEC, modLoaded, &modDeps, 
                        modInfo, flags);
    }

#ifdef	SDSC
    /* These need to be initialized before we call getDDFromSource() ... */
    loaderData.modLoaded = modLoaded;
    loaderData.modDepsPtr = &modDeps;
    loaderData.modInfo = modInfo;
#endif

    if (!access("/dd.img", R_OK)) {
        logMessage("found /dd.img, loading drivers");
        getDDFromSource(&loaderData, "path:/dd.img", flags);
    }
    
    /* this allows us to do an early load of modules specified on the
     * command line to allow automating the load order of modules so that
     * eg, certain scsi controllers are definitely first.
     * FIXME: this syntax is likely to change in a future release
     *        but is done as a quick hack for the present.
     */
    earlyModuleLoad(modInfo, modLoaded, modDeps, 0, flags);
    

    busProbe(modInfo, modLoaded, modDeps, 0, flags);

#ifndef	SDSC
    /* JKFIXME: should probably not be doing this, but ... */
    loaderData.modLoaded = modLoaded;
    loaderData.modDepsPtr = &modDeps;
    loaderData.modInfo = modInfo;
#endif

    /* JKFIXME: we'd really like to do this before the busprobe, but then
     * we won't have network devices available (and that's the only thing
     * we support with this right now */
    if (loaderData.ddsrc != NULL) {
        getDDFromSource(&loaderData, loaderData.ddsrc, flags);
    }


    /* JKFIXME: loaderData->ksFile is set to the arg from the command line,
     * and then getKickstartFile() changes it and sets FL_KICKSTART.  
     * kind of weird. */
    if (loaderData.ksFile || ksFile) {
        logMessage("getting kickstart file");

        if (!ksFile)
#ifdef	SDSC
	{
		/*
		 * If we can't get the kickstart file still let the dialogs
		 * pop up (good for debuging) but reboot the machine and try
		 * again using the watchdog.
		 */
		watchdog_on();
#endif	
            getKickstartFile(&loaderData, &flags);
#ifdef SDSC
		watchdog_off();
	}
#endif	
        if (FL_KICKSTART(flags) && 
            (ksReadCommands((ksFile) ? ksFile : loaderData.ksFile, 
                            flags) != LOADER_ERROR)) {
            runKickstart(&loaderData, &flags);
        }
    }

    if (FL_TELNETD(flags))
        startTelnetd(&loaderData, modInfo, modLoaded, modDeps, flags);

    url = doLoaderMain("/mnt/source", &loaderData, modInfo, modLoaded, &modDeps, flags);

    if (!FL_TESTING(flags)) {
        /* unlink dirs and link to the ones in /mnt/runtime */
        migrate_runtime_directory("/usr");
        migrate_runtime_directory("/lib");
        migrate_runtime_directory("/lib64");
    }

    /* now load SELinux policy before exec'ing anaconda and the shell
     * (if we're using SELinux) */
    if (FL_SELINUX(flags)) {
        if (mount("/selinux", "/selinux", "selinuxfs", 0, NULL)) {
            logMessage("failed to mount /selinux: %s", strerror(errno));
        } else {
            /* FIXME: this is a bad hack for libselinux assuming things
             * about paths */
            symlink("/mnt/runtime/etc/selinux", "/etc/selinux");
            if (loadpolicy() == 0) {
                setexeccon(ANACONDA_CONTEXT);
            } else {
                logMessage("failed to load policy, disabling SELinux");
                flags &= ~LOADER_FLAGS_SELINUX;
            }
        }
    }

    logMessage("getting ready to spawn shell now");
    
    spawnShell(flags);  /* we can attach gdb now :-) */

    /* setup the second stage modules; don't over-ride any already existing
     * modules because that would be rude 
     */
    {
        mlLoadDeps(&modDeps, "/mnt/runtime/modules/modules.dep");
        pciReadDrivers("/mnt/runtime/modules/pcitable");
        readModuleInfo("/mnt/runtime/modules/module-info", modInfo,
                       secondStageModuleLocation, 0);
    }

    /* JKFIXME: kickstart devices crap... probably kind of bogus now though */


    /* we might have already loaded these, but trying again doesn't hurt */
    ideSetup(modLoaded, modDeps, modInfo, flags);
    scsiSetup(modLoaded, modDeps, modInfo, flags);
    busProbe(modInfo, modLoaded, modDeps, 0, flags);

#if !defined(__s390__) && !defined(__s390x__)
    checkForHardDrives(&flags);
#endif

    if ((!canProbeDevices() || FL_ISA(flags) || FL_NOPROBE(flags))
        && !loaderData.ksFile) {
        startNewt(flags);
        manualDeviceCheck(modInfo, modLoaded, &modDeps, flags);
    }
    
    if (FL_UPDATES(flags)) 
        loadUpdates(flags);

    /* look for cards which require the agpgart module */
    agpgartInitialize(modLoaded, modDeps, modInfo, flags);

    mlLoadModuleSetLocation("md:raid0:raid1:raid5:raid6:fat:msdos:jbd:ext3:reiserfs:jfs:xfs:dm-mod:dm-zero:dm-mirror:dm-snapshot",
			    modLoaded, modDeps, modInfo, flags, 
			    secondStageModuleLocation);

    initializeParallelPort(modLoaded, modDeps, modInfo, flags);

    usbInitializeMouse(modLoaded, modDeps, modInfo, flags);

    /* we've loaded all the modules we're going to.  write out a file
     * describing which scsi disks go with which scsi adapters */
    writeScsiDisks(modLoaded);

    /* if we are in rescue mode lets load st.ko for tape support */
    if (FL_RESCUE(flags))
	scsiTapeInitialize(modLoaded, modDeps, modInfo, flags);

    /* we only want to use RHupdates on nfs installs.  otherwise, we'll 
     * use files on the first iso image and not be able to umount it */
    if (!strncmp(url, "nfs:", 4)) {
        logMessage("NFS install method detected, will use RHupdates/");
        useRHupdates = 1;
    } else {
        useRHupdates = 0;
    }

    if (useRHupdates) {
        setenv("PYTHONPATH", "/tmp/updates:/tmp/product:/mnt/source/RHupdates", 1);
        setenv("LD_LIBRARY_PATH", 
               sdupprintf("/tmp/updates:/tmp/product:/mnt/source/RHupdates:%s",
                           LIBPATH), 1);
    } else {
        setenv("PYTHONPATH", "/tmp/updates:/tmp/product", 1);
        setenv("LD_LIBRARY_PATH", 
               sdupprintf("/tmp/updates:/tmp/product:%s", LIBPATH), 1);
    }


    if (!access("/mnt/runtime/usr/lib/libunicode-lite.so.1", R_OK))
        setenv("LD_PRELOAD", "/mnt/runtime/usr/lib/libunicode-lite.so.1", 1);
    if (!access("/mnt/runtime/usr/lib64/libunicode-lite.so.1", R_OK))
        setenv("LD_PRELOAD", "/mnt/runtime/usr/lib64/libunicode-lite.so.1", 1);

    argptr = anacondaArgs;

    if (!access("/tmp/updates/anaconda", X_OK))
        *argptr++ = "/tmp/updates/anaconda";
    else if (useRHupdates && !access("/mnt/source/RHupdates/anaconda", X_OK))
        *argptr++ = "/mnt/source/RHupdates/anaconda";
    else
#ifdef	SDSC
{
	struct stat     buf;

	/*
	 * see if the rocks anaconda exists
	 */
	if ((stat("/tmp/updates/usr/bin/anaconda", &buf) >= 0) &&
						(S_ISREG(buf.st_mode))) {
		*argptr++ = "/tmp/updates/usr/bin/anaconda";
	} else {
		*argptr++ = "/usr/bin/anaconda";
	}
}
#else
        *argptr++ = "/usr/bin/anaconda";
#endif

    /* make sure /tmp/updates exists so that magic in anaconda to */
    /* symlink rhpl/ will work                                    */
    if (access("/tmp/updates", F_OK))
	mkdirChain("/tmp/updates");

    logMessage("Running anaconda script %s", *(argptr-1));
    
    *argptr++ = "-m";
    if (strncmp(url, "ftp:", 4)) {
        *argptr++ = url;
    } else {
        int fd;

        fd = open("/tmp/method", O_CREAT | O_TRUNC | O_RDWR, 0600);
        write(fd, url, strlen(url));
        write(fd, "\r", 1);
        close(fd);
        *argptr++ = "@/tmp/method";
    }

    /* add extra args - this potentially munges extraArgs */
    tmparg = extraArgs;
    while (*tmparg) {
        char *idx;
        
        logMessage("adding extraArg %s", *tmparg);
        idx = strchr(*tmparg, '=');
        if (idx &&  ((idx-*tmparg) < strlen(*tmparg))) {
            *idx = '\0';
            *argptr++ = *tmparg;
            *argptr++ = idx+1;
        } else {
            *argptr++ = *tmparg;
        }
        
        tmparg++;
    }

    if (FL_RESCUE(flags)) {
        *argptr++ = "--rescue";
        if (FL_SERIAL(flags))
            *argptr++ = "--serial";
    } else {
        if (FL_SERIAL(flags))
            *argptr++ = "--serial";
        if (FL_TEXT(flags))
            *argptr++ = "-T";
        else if (FL_GRAPHICAL(flags))
            *argptr++ = "--graphical";
        if (FL_CMDLINE(flags))
            *argptr++ = "-C";
        if (FL_EXPERT(flags))
            *argptr++ = "--expert";
        if (!FL_SELINUX(flags))
            *argptr++ = "--noselinux";
        else if (FL_SELINUX(flags))
            *argptr++ = "--selinux";
        
        if (FL_KICKSTART(flags)) {
            *argptr++ = "--kickstart";
            *argptr++ = loaderData.ksFile;
        }

        if (FL_VIRTPCONSOLE(flags)) {
            *argptr++ = "--virtpconsole";
            *argptr++ = virtpcon;
        }

        if ((loaderData.lang) && !FL_NOPASS(flags)) {
            *argptr++ = "--lang";
            *argptr++ = loaderData.lang;
        }
        
        if ((loaderData.kbd) && !FL_NOPASS(flags)) {
            *argptr++ = "--keymap";
            *argptr++ = loaderData.kbd;
        }
        
        for (i = 0; i < modLoaded->numModules; i++) {
            if (!modLoaded->mods[i].path) continue;
            if (!strcmp(modLoaded->mods[i].path, 
                        "/mnt/runtime/modules/modules.cgz")) {
                continue;
            }
            
            *argptr++ = "--module";
            *argptr = alloca(80);
            sprintf(*argptr, "%s:%s", modLoaded->mods[i].path,
                    modLoaded->mods[i].name);
            
            argptr++;
        }
    }
    
    *argptr = NULL;
    
    stopNewt();
    closeLog();
    
    if (!FL_TESTING(flags)) {
        int pid, status, rc;
        char * buf;

        if (FL_RESCUE(flags))
            buf = sdupprintf(_("Running anaconda, the %s rescue mode - please wait...\n"), getProductName());
        else
            buf = sdupprintf(_("Running anaconda, the %s system installer - please wait...\n"), getProductName());
        printf("%s", buf);

        if (!(pid = fork())) {
            execv(anacondaArgs[0], anacondaArgs);
            fprintf(stderr, "exec of anaconda failed: %s", strerror(errno));
            exit(1);
        }

        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status))
            rc = 1;
        else
            rc = 0;

        if ((rc == 0) && (FL_POWEROFF(flags) || FL_HALT(flags))) {
            if (!(pid = fork())) {
                char * cmd = (FL_POWEROFF(flags) ? strdup("/sbin/poweroff") :
                              strdup("/sbin/halt"));
                execl(cmd, cmd, NULL);
                fprintf(stderr, "exec of poweroff failed: %s", 
                        strerror(errno));
                exit(1);
            }
            waitpid(pid, &status, 0);
        }

#if defined(__s390__) || defined(__s390x__)
        /* FIXME: we have to send a signal to linuxrc on s390 so that shutdown
         * can happen.  this is ugly */
        FILE * f;
        f = fopen("/var/run/init.pid", "r");
        if (!f) {
            logMessage("can't find init.pid, guessing that init is pid 1");
            pid = 1;
        } else {
            char * buf = malloc(256);
            fgets(buf, 256, f);
            pid = atoi(buf);
        }
        kill(pid, SIGUSR1);
        return rc;
#else
        return rc;
#endif
    }
#if 0
    else {
	char **args = anacondaArgs;
	printf("would have run ");
	while (*args)
	    printf("%s ", *args++);
	printf("\n");
	printf("LANGKEY=%s\n", getenv("LANGKEY"));
	printf("LANG=%s\n", getenv("LANG"));
    }
#endif
    return 1;
}
