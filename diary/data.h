/* data.h: what is in the diary (data.c) */

#define FILE_MAX (1024L * 1024)     /* a text file is read up to this */
#define MENT_MAX 1024
#define FIELDS_MAX 6

enum { sk_me, sk_person, sk_place };
enum { ev_once, ev_range, ev_weekly, ev_monthly, ev_yearly };

/* you (subjs[0]), a person or a place */
struct subj {
    int kind;
    char slug[NAME_MAX_D];
    char name[96];
    char summary[160];
    char fields[FIELDS_MAX][24];
    char values[FIELDS_MAX][96];
    int nfields;
    int nfiles;
    int seen;              /* days that are about it or mention it */
    int last;              /* the newest of them */
};

/* a day file: yours (owner 0) or one about a person or place */
struct dayent {
    int date;
    int owner;
    int words;
    char first[100];
    long ment;             /* " @mira #bakery " in pool */
};

/* a line of an events.txt */
struct event {
    int owner;
    int kind;
    int from, to;          /* ev_once, ev_range */
    int arg;               /* weekly: bit per weekday; monthly: day;
                              yearly: mmdd */
    int line;
    char text[120];
    char ment[200];
};

extern struct subj *subjs;
extern int nsubj;
extern struct dayent *days;
extern int ndays;
extern struct event *events;
extern int nevents;
extern char *pool;

void data_load(void);
void data_untxt(void);
void subj_dir(char *out, int s);
int find_subj(int kind, const char *slug);
int find_day(int owner, int date);
int day_mentions(int i, int s);
int event_on(const struct event *e, int date);
int event_for(const struct event *e, int s);

int lower(int c);
int is_slug_char(int c);
int is_space(int c);
long read_file(const char *path, char *buf, long size);

int days_in_month(int y, int m);
long day_number(int date);
int from_number(long z);
int date_add(int date, long n);
int date_wday(int date);
int date_today(void);
int parse_date(const char *s);
void format_date(char *s, int date);
const char *wday_name(int w);
const char *month_name(int m);
