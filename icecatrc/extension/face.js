/*
 * Shared by background.js and the page script, so that the Tor
 * circuit and the page profile of a site always come from one key.
 */

/*
 * The OS is never made up: workers, fonts, scrollbars and the TCP
 * stack tell the real one, so a made-up OS is a lie anyone can catch.
 * A profile is the real OS with its own screen, cores and noise.
 */
var FACE_OS_NAME = { win: "Windows", mac: "Mac", linux: "Linux" };
var FACE_SCREEN = [
    "1920x1080", "1920x1080", "1366x768", "1536x864", "1440x900",
    "2560x1440", "1600x900", "1280x1024"
];
var FACE_CORES = [2, 4, 4, 8];

function fnv(s)
{
    var h;
    var i;

    h = 0x811c9dc5;
    for (i = 0; i < s.length; i++) {
        h = (h ^ s.charCodeAt(i)) >>> 0;
        h = Math.imul(h, 0x01000193) >>> 0;
    }
    return h;
}

/* FNV alone mixes its low bits poorly; finish with an avalanche */
function hash(s)
{
    var h;

    h = fnv(s);
    h = (h ^ (h >>> 16)) >>> 0;
    h = Math.imul(h, 0x85ebca6b) >>> 0;
    h = (h ^ (h >>> 13)) >>> 0;
    h = Math.imul(h, 0xc2b2ae35) >>> 0;
    h = (h ^ (h >>> 16)) >>> 0;
    return h;
}

/*
 * Kept sites have their own lasting secret, so that they keep one
 * profile across restarts; every other site uses the session secret,
 * which dies at quit.
 */
function face_secret(faces, site, secret)
{
    var k;

    for (k in faces) {
        if (site == k || site.endsWith("." + k)) {
            return faces[k];
        }
    }
    return secret;
}

function face_seed(secret, gen, n, site)
{
    return secret + "g" + gen + "." + n + "s" + site;
}

function face_pick(seed, list, salt)
{
    return list[hash(seed + "|" + salt) % list.length];
}

function face_of(seed, os)
{
    var size;
    var bar;

    if (!FACE_OS_NAME[os]) {
        os = "linux";
    }
    size = face_pick(seed, FACE_SCREEN, "screen").split("x");
    bar = 40;
    if (os == "mac") {
        bar = 25;
    }
    if (os == "linux") {
        bar = 28;
    }
    return {
        os: os,
        width: parseInt(size[0], 10),
        height: parseInt(size[1], 10),
        bar: bar,
        cores: face_pick(seed, FACE_CORES, "cores")
    };
}

/* With resistFingerprinting on, screen and cores are its own */
function face_text(f, rfp)
{
    if (rfp) {
        return FACE_OS_NAME[f.os] + ", screen and cores as RFP says, " +
            "own noise";
    }
    return FACE_OS_NAME[f.os] + ", " + f.width + "x" + f.height + ", " +
        f.cores + " cores";
}
