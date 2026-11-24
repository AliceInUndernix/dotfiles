/* WebArchiver defaults; copy to config.h and edit, then rebuild.
 * Every value can also be changed per run with an environment variable
 * of the same name in lower case shown next to it. */

/* domains downloaded at the same time (JOBS) */
#define DEF_JOBS 10

/* seconds between requests to one site: start with the first step, move
 * to the next one when the site pushes back (WAIT_STEPS) */
#define DEF_WAIT_STEPS "0.2 1 3"

/* a slowed-down site keeps its slower pace for this many days */
#define SLOW_DAYS 7

/* seconds between progress updates (REFRESH) */
#define DEF_REFRESH 2

/* extensions of "big files": checked for duplicates before downloading
 * (BIG_EXT) */
#define DEF_BIG_EXT "mp4,m4v,webm,mkv,mov,avi,flv,wmv,mpg,mpeg,ts,3gp," \
    "ogv,f4v,mp3,m4a,flac,wav,ogg,oga,opus,aac,wma,zip,rar,7z,tar,gz," \
    "tgz,bz2,xz,zst,iso,img,dmg,exe,msi,apk,deb,rpm,pdf"

/* files a page needs to look right; fetched afterwards if wget missed
 * them */
#define REQ_EXTS "css,js,png,jpg,jpeg,gif,svg,webp,avif,ico,bmp,woff," \
    "woff2,ttf,otf,eot"

/* a common browser identity: a rare one would mark you on every site
 * (UA) */
#define DEF_USER_AGENT \
    "Mozilla/5.0 (Windows NT 10.0; rv:128.0) Gecko/20100101 Firefox/128.0"

/* bytes compared at the start of a big file that has no ETag */
#define PREFIX_BYTES 1048576

/* port of the local archive page (--serve) */
#define SERVE_PORT 8800

/* .onion links, and everything an .onion site needs, are fetched by
 * wget started through this command, so they go through Tor; it is
 * never left out (TOR_CMD). The dotfiles' proxychains gives wget a Tor
 * port of its own. */
#define DEF_TOR_CMD "proxychains -q"

/* files of the dotfiles' vpn (net/vpn), read to say why Tor does not
 * answer: "<state> <level>", and the "vpn off" mark */
#define VPN_STATE_FILE "/run/vpn/state"
#define VPN_OFF_FILE "/etc/vpn/off"
