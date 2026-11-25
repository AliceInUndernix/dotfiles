var page = window.wrappedJSObject;
var cfg = switch_cfg;

var site = top_host();
var seed = face_seed(face_secret(cfg.faces, site, cfg.secret), cfg.gen,
    cfg.site_gen[site] || 0, site);

/*
 * The site in the address bar, as background.js sees it for the
 * circuit and the headers. A frame from another site cannot read
 * top.location; then the frame's ancestors are asked, and failing
 * that the address that embedded it. A frame must never use its own
 * host: the same tracker frame would then look alike on every site.
 */
function top_host()
{
    var a;
    var r;

    try {
        return window.top.location.hostname;
    } catch (e) {
    }
    try {
        a = window.location.ancestorOrigins;
        if (a && a.length > 0) {
            return new URL(a[a.length - 1]).hostname;
        }
    } catch (e) {
    }
    try {
        r = document.referrer;
        if (r != "") {
            return new URL(r).hostname;
        }
    } catch (e) {
    }
    return window.location.hostname;
}

function rnd(salt)
{
    return hash(seed + "|" + salt);
}

function guard(fn)
{
    try {
        fn();
    } catch (e) {
    }
}

function proto(name)
{
    var c;

    c = page[name];
    if (!c) {
        return null;
    }
    return c.prototype;
}

function def_get(obj, name, fn)
{
    var d;

    if (!obj) {
        return;
    }
    d = new page.Object();
    d.get = exportFunction(fn, window);
    d.configurable = true;
    d.enumerable = true;
    page.Object.defineProperty(obj, name, d);
}

function def_value(obj, name, value)
{
    def_get(obj, name, function () {
        return value;
    });
}

function def_fn(obj, name, fn)
{
    if (!obj) {
        return;
    }
    exportFunction(fn, obj, { defineAs: name });
}

function page_list(a)
{
    return cloneInto(a, window);
}

function resolved(value)
{
    return page.Promise.resolve(value);
}

function lie_identity(ua, platform, oscpu, appversion)
{
    var nav;

    if (ua == "") {
        return;
    }
    nav = proto("Navigator");
    def_value(nav, "userAgent", ua);
    def_value(nav, "platform", platform);
    def_value(nav, "oscpu", oscpu);
    def_value(nav, "appVersion", appversion);
}

function lie_language()
{
    var nav;

    nav = proto("Navigator");
    def_value(nav, "language", "en-US");
    def_get(nav, "languages", function () {
        return page_list(["en-US", "en"]);
    });
}

/* A screen smaller than the window is a lie anyone can catch */
function lie_screen(w, h, bar)
{
    var s;

    s = proto("Screen");
    def_get(s, "width", function () {
        return Math.max(w, window.innerWidth);
    });
    def_get(s, "height", function () {
        return Math.max(h, window.innerHeight + bar);
    });
    def_get(s, "availWidth", function () {
        return Math.max(w, window.innerWidth);
    });
    def_get(s, "availHeight", function () {
        return Math.max(h, window.innerHeight + bar) - bar;
    });
    def_value(s, "availLeft", 0);
    def_value(s, "availTop", 0);
    def_value(s, "colorDepth", 24);
    def_value(s, "pixelDepth", 24);
    def_value(page, "devicePixelRatio", 1);
    def_value(page, "screenX", 0);
    def_value(page, "screenY", 0);
    def_value(page, "mozInnerScreenX", 0);
    def_value(page, "mozInnerScreenY", 0);
    def_get(page, "outerWidth", function () {
        return window.innerWidth;
    });
    def_get(page, "outerHeight", function () {
        return window.innerHeight;
    });
}

function hide_webgl(name)
{
    var c;
    var orig;

    c = window[name];
    if (!c) {
        return;
    }
    orig = c.prototype.getContext;
    def_fn(proto(name), "getContext", function (type, opts) {
        if (String(type).toLowerCase().indexOf("webgl") >= 0) {
            return null;
        }
        return orig.call(this, type, opts);
    });
}

function hide_hardware()
{
    guard(function () {
        hide_webgl("HTMLCanvasElement");
    });
    guard(function () {
        hide_webgl("OffscreenCanvas");
    });
    def_fn(proto("MediaDevices"), "enumerateDevices", function () {
        return resolved(page_list([]));
    });
    def_fn(proto("Navigator"), "getGamepads", function () {
        return page_list([]);
    });
    def_fn(proto("SpeechSynthesis"), "getVoices", function () {
        return page_list([]);
    });
    def_fn(proto("StorageManager"), "estimate", function () {
        return resolved(cloneInto({ quota: 2147483648, usage: 0 }, window));
    });
}

function copy_canvas(c)
{
    var b;

    b = document.createElement("canvas");
    b.width = c.width;
    b.height = c.height;
    return b;
}

function noisy_canvas(c, x, y, w, h)
{
    var b;
    var ctx;
    var i;
    var px;
    var py;

    b = copy_canvas(c);
    if (b.width == 0 || b.height == 0) {
        return b;
    }
    ctx = b.getContext("2d");
    ctx.drawImage(c, 0, 0);
    if (w < 0) {
        x = x + w;
        w = -w;
    }
    if (h < 0) {
        y = y + h;
        h = -h;
    }
    w = Math.floor(w);
    h = Math.floor(h);
    if (w <= 0 || h <= 0) {
        return b;
    }
    for (i = 0; i < 4; i++) {
        px = Math.floor(x) + rnd("cx" + i) % w;
        py = Math.floor(y) + rnd("cy" + i) % h;
        ctx.fillStyle = "rgba(" + rnd("cr" + i) % 256 + "," +
            rnd("cg" + i) % 256 + "," + rnd("cb" + i) % 256 + ",0.03)";
        ctx.fillRect(px, py, 1, 1);
    }
    return b;
}

function canvas_hooks(noise)
{
    var to_url;
    var to_blob;
    var get_data;
    var make_data;

    to_url = window.HTMLCanvasElement.prototype.toDataURL;
    to_blob = window.HTMLCanvasElement.prototype.toBlob;
    get_data = window.CanvasRenderingContext2D.prototype.getImageData;
    make_data = window.CanvasRenderingContext2D.prototype.createImageData;
    def_fn(proto("HTMLCanvasElement"), "toDataURL", function (type, q) {
        var b;

        if (noise) {
            b = noisy_canvas(this, 0, 0, this.width, this.height);
        } else {
            b = copy_canvas(this);
        }
        return to_url.call(b, type, q);
    });
    def_fn(proto("HTMLCanvasElement"), "toBlob", function (cb, type, q) {
        var b;

        if (noise) {
            b = noisy_canvas(this, 0, 0, this.width, this.height);
        } else {
            b = copy_canvas(this);
        }
        to_blob.call(b, cb, type, q);
    });
    def_fn(proto("CanvasRenderingContext2D"), "getImageData",
        function (x, y, w, h) {
            var b;

            if (!noise) {
                return make_data.call(this, w, h);
            }
            b = noisy_canvas(this.canvas, x, y, w, h);
            return get_data.call(b.getContext("2d"), x, y, w, h);
        });
}

function noisy_audio()
{
    var get_ch;
    var copy_from;
    var get_freq;
    var done;
    var delta;
    var start;

    get_ch = window.AudioBuffer.prototype.getChannelData;
    copy_from = window.AudioBuffer.prototype.copyFromChannel;
    get_freq = window.AnalyserNode.prototype.getFloatFrequencyData;
    done = new WeakSet();
    delta = (1 + rnd("ad") % 9) * 1e-7;
    start = rnd("as") % 100;

    function touch(buf, ch)
    {
        var d;
        var i;

        d = get_ch.call(buf, ch);
        if (done.has(d)) {
            return d;
        }
        done.add(d);
        for (i = start; i < d.length; i += 100) {
            d[i] = d[i] + delta;
        }
        return d;
    }

    def_fn(proto("AudioBuffer"), "getChannelData", function (ch) {
        return touch(this, ch);
    });
    def_fn(proto("AudioBuffer"), "copyFromChannel",
        function (dest, ch, off) {
            touch(this, ch);
            copy_from.call(this, dest, ch, off || 0);
        });
    def_fn(proto("AnalyserNode"), "getFloatFrequencyData", function (a) {
        var i;

        get_freq.call(this, a);
        for (i = start % 10; i < a.length; i += 10) {
            a[i] = a[i] + delta * 1000;
        }
    });
}

function hide_audio()
{
    delete page.AudioContext;
    delete page.OfflineAudioContext;
    delete page.webkitAudioContext;
}

function geo_answer(ok, err)
{
    var v;

    if (cfg.geo == "fake" && typeof ok == "function") {
        v = cloneInto({
            coords: {
                latitude: cfg.geo_lat,
                longitude: cfg.geo_lon,
                accuracy: 50,
                altitude: null,
                altitudeAccuracy: null,
                heading: null,
                speed: null
            },
            timestamp: Date.now()
        }, window);
        window.setTimeout(function () {
            ok(v);
        }, 40);
        return;
    }
    if (typeof err != "function") {
        return;
    }
    v = cloneInto({
        code: 1,
        message: "User denied Geolocation",
        PERMISSION_DENIED: 1,
        POSITION_UNAVAILABLE: 2,
        TIMEOUT: 3
    }, window);
    window.setTimeout(function () {
        err(v);
    }, 40);
}

function lie_geo()
{
    var g;

    g = proto("Geolocation");
    def_fn(g, "getCurrentPosition", function (ok, err) {
        geo_answer(ok, err);
    });
    def_fn(g, "watchPosition", function (ok, err) {
        geo_answer(ok, err);
        return 1;
    });
    def_fn(g, "clearWatch", function () {
    });
}

function hide_referrer()
{
    var get;

    get = Object.getOwnPropertyDescriptor(window.Document.prototype,
        "referrer").get;
    def_get(proto("Document"), "referrer", function () {
        var r;

        r = get.call(this);
        if (r == "") {
            return r;
        }
        try {
            if (new URL(r).hostname != window.location.hostname) {
                return "";
            }
        } catch (e) {
            return "";
        }
        return r;
    });
}

/*
 * With resistFingerprinting on, screen and cores stay as it reports
 * them: it reports them the same way to CSS and to workers, where no
 * lie of ours can reach, so a lie here would contradict them.
 */
function lie_face()
{
    var f;

    f = face_of(seed, cfg.os);
    guard(function () {
        lie_identity(cfg[f.os + "_ua"], cfg[f.os + "_platform"],
            cfg[f.os + "_oscpu"], cfg[f.os + "_appversion"]);
    });
    if (!cfg.rfp) {
        guard(function () {
            lie_screen(f.width, f.height, f.bar);
        });
        guard(function () {
            def_value(proto("Navigator"), "hardwareConcurrency", f.cores);
        });
    }
    if (cfg.canvas) {
        guard(function () {
            canvas_hooks(1);
        });
    }
    if (cfg.audio) {
        guard(noisy_audio);
    }
}

function lie_uniform()
{
    if (cfg.ua) {
        guard(function () {
            lie_identity(cfg.ua_str, cfg.ua_platform, cfg.ua_oscpu,
                cfg.ua_appversion);
        });
    }
    if (cfg.screen) {
        guard(function () {
            lie_screen(1920, 1080, 40);
        });
    }
    if (!cfg.rfp) {
        guard(function () {
            def_value(proto("Navigator"), "hardwareConcurrency", 2);
        });
    }
    if (cfg.canvas) {
        guard(function () {
            canvas_hooks(0);
        });
    }
    if (cfg.audio) {
        guard(hide_audio);
    }
}

function lie_all()
{
    if (cfg.fp == "profile") {
        lie_face();
    } else {
        lie_uniform();
    }
    if (cfg.lang) {
        guard(lie_language);
    }
    if (cfg.hw) {
        guard(hide_hardware);
    }
    if (cfg.geo != "allow") {
        guard(lie_geo);
    }
    if (cfg.ref) {
        guard(hide_referrer);
    }
}

/*
 * For a moment, while background.js swaps registrations, two copies
 * of this script can run in one frame; only the first one lies.
 */
if (typeof switch_ran == "undefined") {
    var switch_ran = 1;
    lie_all();
}
