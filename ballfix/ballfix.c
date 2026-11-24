/* ballfix: a filter for the buttons of a trackball (or any mouse).
 * It reads the device, stops the false double clicks of a worn switch,
 * changes the buttons you name, and passes everything else on through
 * a new device. Plain C, only the kernel: evdev and uinput.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define DEFAULT_DEVICE "/dev/input/by-id/usb-Kensington_SlimBlade_Pro_Tr" \
    "ackball_Wired__Kensington_SlimBlade_Pro_Trackball_Wired_-event-mouse"
#define DEFAULT_DELAY 40
#define BTN_FIRST 0x110
#define BTN_COUNT 16
#define KEYBYTES (KEY_MAX / 8 + 1)
#define EVBATCH 64

struct button_name {
    const char *name;
    int code;
};

static struct button_name names[] = {
    { "left", 0x110 }, { "right", 0x111 }, { "middle", 0x112 },
    { "side", 0x113 }, { "extra", 0x114 }, { "forward", 0x115 },
    { "back", 0x116 }, { "task", 0x117 }, { 0, 0 }
};

static int out_fd;
static int verbose;
static int delay_ms;
static int map[BTN_COUNT];
static int down[BTN_COUNT];
static int pending[BTN_COUNT];
static long long due[BTN_COUNT];

static int find_button(const char *s)
{
    int i;

    for (i = 0; names[i].name; i++) {
        if (strcmp(names[i].name, s) == 0) {
            return names[i].code;
        }
    }
    return -1;
}

static const char *button_label(int code)
{
    static char buf[16];
    int i;

    for (i = 0; names[i].name; i++) {
        if (names[i].code == code) {
            return names[i].name;
        }
    }
    sprintf(buf, "code%d", code);
    return buf;
}

static long long now_ms(void)
{
    struct timespec ts;
    int res;

    res = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (res < 0) {
        return 0;
    }
    return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int number(const char *s)
{
    int n;

    n = 0;
    if (*s == 0) {
        return -1;
    }
    while (*s) {
        if (*s < '0' || *s > '9' || n > 100000) {
            return -1;
        }
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

static int emit(int type, int code, int value)
{
    struct input_event ev;
    int res;

    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    res = write(out_fd, &ev, sizeof ev);
    if (res != (int) sizeof ev) {
        return -1;
    }
    return 0;
}

static int release_now(int idx)
{
    int res;

    pending[idx] = 0;
    down[idx] = 0;
    res = emit(EV_KEY, BTN_FIRST + idx, 0);
    if (res < 0) {
        return -1;
    }
    return emit(EV_SYN, SYN_REPORT, 0);
}

/* One event from the device. Returns -1 when writing fails. */
static int handle(struct input_event *ev, long long now)
{
    int code, idx;

    if (ev->type != EV_KEY || ev->code < BTN_FIRST ||
        ev->code >= BTN_FIRST + BTN_COUNT)
    {
        return emit(ev->type, ev->code, ev->value);
    }
    code = map[ev->code - BTN_FIRST];
    idx = code - BTN_FIRST;
    if (verbose) {
        fprintf(stderr, "%s %s", ev->value ? "press  " : "release",
            button_label(ev->code));
        if (code != ev->code) {
            fprintf(stderr, " -> %s", button_label(code));
        }
        fprintf(stderr, "\n");
    }
    if (ev->value == 1) {
        if (pending[idx]) {
            /* released and pressed again at once: a bounce */
            pending[idx] = 0;
            if (verbose) {
                fprintf(stderr, "bounce  %s kept down (%d ms)\n",
                    button_label(code), (int) (now - due[idx] + delay_ms));
            }
            return 0;
        }
        down[idx] = 1;
        return emit(EV_KEY, code, 1);
    }
    if (ev->value == 0) {
        if (!down[idx] || pending[idx]) {
            return 0;
        }
        if (delay_ms == 0) {
            return release_now(idx);
        }
        pending[idx] = 1;
        due[idx] = now + delay_ms;
        return 0;
    }
    return emit(EV_KEY, code, ev->value);
}

static int setup_output(int in, int *targets)
{
    unsigned char bits[KEYBYTES];
    struct uinput_setup us;
    struct input_id id;
    int fd, i, t, res;

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "ballfix: /dev/uinput: %s\n", strerror(errno));
        return -1;
    }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_EVBIT, EV_MSC);
    memset(bits, 0, sizeof bits);
    ioctl(in, EVIOCGBIT(EV_KEY, sizeof bits), bits);
    for (i = 0; i < KEY_MAX; i++) {
        if (bits[i >> 3] & (1 << (i & 7))) {
            ioctl(fd, UI_SET_KEYBIT, i);
        }
    }
    for (i = 0; i < BTN_COUNT; i++) {
        t = targets[i];
        ioctl(fd, UI_SET_KEYBIT, t);
    }
    memset(bits, 0, sizeof bits);
    ioctl(in, EVIOCGBIT(EV_REL, sizeof bits), bits);
    for (i = 0; i < REL_MAX; i++) {
        if (bits[i >> 3] & (1 << (i & 7))) {
            ioctl(fd, UI_SET_RELBIT, i);
        }
    }
    memset(bits, 0, sizeof bits);
    ioctl(in, EVIOCGBIT(EV_MSC, sizeof bits), bits);
    for (i = 0; i < MSC_MAX; i++) {
        if (bits[i >> 3] & (1 << (i & 7))) {
            ioctl(fd, UI_SET_MSCBIT, i);
        }
    }
    memset(&us, 0, sizeof us);
    res = ioctl(in, EVIOCGID, &id);
    if (res == 0) {
        us.id = id;
    }
    strcpy(us.name, "ballfix");
    res = ioctl(fd, UI_DEV_SETUP, &us);
    if (res < 0) {
        fprintf(stderr, "ballfix: uinput setup: %s\n", strerror(errno));
        return -1;
    }
    res = ioctl(fd, UI_DEV_CREATE);
    if (res < 0) {
        fprintf(stderr, "ballfix: uinput create: %s\n", strerror(errno));
        return -1;
    }
    return fd;
}

static void usage(void)
{
    puts("usage: ballfix [-v] [-d DEVICE] [-t MS] [-m FROM=TO] ...");
    puts("  -d DEVICE  event device (default: the Kensington SlimBlade)");
    puts("  -t MS      ignore a release followed by a press within MS");
    puts("             milliseconds (default 40; 0 turns it off)");
    puts("  -m FROM=TO make button FROM act as button TO; names:");
    puts("             left right middle side extra forward back task");
    puts("  -v         say on stderr every button event and every bounce");
}

int main(int argc, char **argv)
{
    struct input_event evs[EVBATCH];
    struct pollfd pfd;
    const char *device;
    char *eq;
    char spec[64];
    long long now, wait;
    int in, i, n, res, from, to, tmo, targets[BTN_COUNT];

    device = DEFAULT_DEVICE;
    delay_ms = DEFAULT_DELAY;
    verbose = 0;
    for (i = 0; i < BTN_COUNT; i++) {
        map[i] = BTN_FIRST + i;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            i++;
            device = argv[i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            i++;
            delay_ms = number(argv[i]);
            if (delay_ms < 0) {
                fprintf(stderr, "ballfix: bad time: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            i++;
            if (strlen(argv[i]) >= sizeof spec) {
                fprintf(stderr, "ballfix: bad -m: %s\n", argv[i]);
                return 2;
            }
            strcpy(spec, argv[i]);
            eq = strchr(spec, '=');
            if (!eq) {
                fprintf(stderr, "ballfix: bad -m: %s\n", argv[i]);
                return 2;
            }
            *eq = 0;
            from = find_button(spec);
            to = find_button(eq + 1);
            if (from < 0 || to < 0) {
                fprintf(stderr, "ballfix: bad button in -m %s\n", argv[i]);
                return 2;
            }
            map[from - BTN_FIRST] = to;
        } else {
            usage();
            return strcmp(argv[i], "-h") == 0 ? 0 : 2;
        }
    }
    for (i = 0; i < BTN_COUNT; i++) {
        targets[i] = map[i];
    }
    in = open(device, O_RDONLY);
    if (in < 0) {
        fprintf(stderr, "ballfix: %s: %s\n", device, strerror(errno));
        return 1;
    }
    out_fd = setup_output(in, targets);
    if (out_fd < 0) {
        return 1;
    }
    res = ioctl(in, EVIOCGRAB, 1);
    if (res < 0) {
        fprintf(stderr, "ballfix: cannot grab %s: %s\n", device,
            strerror(errno));
        return 1;
    }
    pfd.fd = in;
    pfd.events = POLLIN;
    for (;;) {
        tmo = -1;
        now = now_ms();
        for (i = 0; i < BTN_COUNT; i++) {
            if (pending[i]) {
                wait = due[i] - now;
                if (wait < 0) {
                    wait = 0;
                }
                if (tmo < 0 || wait < tmo) {
                    tmo = (int) wait;
                }
            }
        }
        res = poll(&pfd, 1, tmo);
        if (res < 0 && errno == EINTR) {
            continue;
        }
        if (res < 0) {
            break;
        }
        now = now_ms();
        for (i = 0; i < BTN_COUNT; i++) {
            if (pending[i] && due[i] <= now) {
                res = release_now(i);
                if (res < 0) {
                    return 1;
                }
            }
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }
        if (pfd.revents & POLLIN) {
            n = read(in, evs, sizeof evs);
            if (n < 0 && errno != EAGAIN && errno != EINTR) {
                break;
            }
            for (i = 0; i * (int) sizeof evs[0] < n; i++) {
                res = handle(&evs[i], now);
                if (res < 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
