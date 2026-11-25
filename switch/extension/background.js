var TOR_HOST = "127.0.0.1";
/* the dotfiles' torrc keeps port 9150 for IceCat (net/README) */
var TOR_PORT = 9150;
var IP_URL = "https://check.torproject.org/api/ip";
var LANG_VALUE = "en-US,en;q=0.5";
var CSP_VALUE = "script-src 'none'; object-src 'none'";

var STRIP_PREFIX = ["utm_", "mtm_", "pk_", "hsa_"];
var STRIP_NAME = [
    "fbclid", "gclid", "dclid", "gbraid", "wbraid", "msclkid", "yclid",
    "igshid", "mc_eid", "mc_cid", "_hsenc", "_hsmi", "mkt_tok", "twclid",
    "ttclid", "vero_id", "wickedid", "ref_src", "si", "_ga", "_gl",
    "oly_enc_id", "oly_anon_id", "s_cid", "spm"
];

var REDIRECTS = [
    "www.google.com /url q",
    "www.google.com /url url",
    "l.facebook.com /l.php u",
    "lm.facebook.com /l.php u",
    "www.youtube.com /redirect q",
    "away.vk.com /away.php to",
    "steamcommunity.com /linkfilter/ url",
    "steamcommunity.com /linkfilter/ u",
    "out.reddit.com / url",
    "duckduckgo.com /l/ uddg",
    "slack-redir.net /link url"
];

var DEF = {
    js: 1, tor: 0, gen: 0, sites: "", iso: "both", wipe_switch: 1,
    fp: "profile", webrtc: 1, lan: 1, rfp: 1, ua: "default", lang: 1,
    screen: 0, hw: 1, canvas: 1, audio: 1, geo: "deny",
    geo_lat: "51.5074", geo_lon: "-0.1278", exempt: "", ref: 1,
    strip: 1, skip: 1, beacon: 1, etag: 1, fonts: 0, cookies: 1,
    fpi: 1, audit: 1, predict: 1, forget: 0, keep: "", wipe: 1,
    hist: "unkept", hist_days: "7", cache_min: "60", dl: 1, forms: 1,
    closed: 1, seen: "", faces: "", auto_ip: 1,
    https: 1, http_ok: ""
};

var STORAGE_CODE = "(function () {" +
    "var out = { local: [], session: [], idb: [] };" +
    "function grab(s, a) {" +
    "var i; var k;" +
    "for (i = 0; i < s.length; i++) {" +
    "k = s.key(i); a.push([k, String(s.getItem(k))]); } }" +
    "try { grab(localStorage, out.local); } catch (e) {}" +
    "try { grab(sessionStorage, out.session); } catch (e) {}" +
    "if (!indexedDB.databases) { return out; }" +
    "return indexedDB.databases().then(function (d) {" +
    "var i;" +
    "for (i = 0; i < d.length; i++) {" +
    "out.idb.push(d[i].name + ' (version ' + d[i].version + ')'); }" +
    "return out; }, function () { return out; });" +
    "})();";

var state = { loaded: 0 };
var fx_major = 0;
var reg = null;
var tab_host = {};
var secret = make_secret();
var site_gen = {};
var ip_wait = {};
var ip_cache = {};
/* onStartup does not fire on the first start after an update */
var want_wipe = 1;
var proxy_ok = 0;
var rivals = "";
var ip_busy = {};
var http_once = {};
var http_wait = {};
var http_tries = {};
/* the real OS (runtime.getPlatformInfo); profiles never change it */
var real_os = "linux";
/* the page lies are registered; until then pages get no JavaScript */
var script_ok = 0;

/* Tor builds a new circuit after ten minutes; look again after that */
var IP_AGE = 600000;

function make_secret()
{
    var a;

    a = new Uint32Array(4);
    crypto.getRandomValues(a);
    return a.join("-");
}

function site_key(site)
{
    return face_seed(face_secret(face_map(), site, secret), state.gen,
        site_gen[site] || 0, site);
}

/* state.faces is flat text: one "host secret" pair per line */
function face_map()
{
    var a;
    var m;
    var i;
    var p;

    m = {};
    a = lines_of(state.faces);
    for (i = 0; i < a.length; i++) {
        p = a[i].split(" ");
        if (p.length == 2) {
            m[p[0]] = p[1];
        }
    }
    return m;
}

/* Give each kept host a lasting secret; forget those not kept */
function sync_faces()
{
    var m;
    var keep;
    var out;
    var i;
    var text;

    m = face_map();
    keep = lines_of(state.keep);
    out = [];
    for (i = 0; i < keep.length; i++) {
        if (keep[i].indexOf(" ") < 0) {
            out.push(keep[i] + " " + (m[keep[i]] || make_secret()));
        }
    }
    text = out.join("\n");
    if (text == state.faces) {
        return;
    }
    state.faces = text;
    browser.storage.local.set({ faces: text });
    register_script();
}

/* At quit, unkept sites lose their profiles along with their data */
function forget_faces()
{
    var h;

    secret = make_secret();
    for (h in site_gen) {
        if (!list_has(state.keep, h)) {
            delete site_gen[h];
        }
    }
    register_script();
}

function reset_faces()
{
    state.faces = "";
    sync_faces();
}

function auto_ip(host)
{
    var c;

    if (!state.auto_ip || !state.tor || host == "" || ip_busy[host] ||
        no_exit(host))
    {
        return;
    }
    c = ip_cache[host];
    if (c && Date.now() - c.time < IP_AGE) {
        return;
    }
    ip_busy[host] = 1;
    lookup_ip(host).then(function () {
        delete ip_busy[host];
    });
}

function init_state()
{
    var k;

    for (k in DEF) {
        state[k] = DEF[k];
    }
}

function snapshot()
{
    var r;
    var k;

    r = {};
    for (k in DEF) {
        r[k] = state[k];
    }
    return r;
}

function noop()
{
}

function empty()
{
    return [];
}

function host_match(h, host)
{
    return h == host || h.endsWith("." + host);
}

function hostname(url)
{
    try {
        return new URL(url).hostname;
    } catch (e) {
        return "";
    }
}

function lines_of(text)
{
    var a;
    var out;
    var i;
    var e;

    a = text.split("\n");
    out = [];
    for (i = 0; i < a.length; i++) {
        e = a[i].trim();
        if (e != "") {
            out.push(e);
        }
    }
    return out;
}

function list_has(text, host)
{
    var a;
    var i;

    if (host == "") {
        return 0;
    }
    a = lines_of(text);
    for (i = 0; i < a.length; i++) {
        if (host == a[i] || host.endsWith("." + a[i])) {
            return 1;
        }
    }
    return 0;
}

function edit_list(key, host, add)
{
    var a;
    var out;
    var i;

    a = lines_of(state[key]);
    out = [];
    for (i = 0; i < a.length; i++) {
        if (a[i] != host) {
            out.push(a[i]);
        }
    }
    if (add && host != "") {
        out.push(host);
    }
    state[key] = out.join("\n");
}

function is_private(host)
{
    var m;
    var a;
    var b;

    if (host == "localhost" || host.endsWith(".localhost")) {
        return 1;
    }
    if (host.indexOf("[") == 0) {
        if (host == "[::1]" || host.indexOf("[fc") == 0 ||
            host.indexOf("[fd") == 0 || host.indexOf("[fe80") == 0)
        {
            return 1;
        }
        return 0;
    }
    m = host.match(/^(\d+)\.(\d+)\.\d+\.\d+$/);
    if (!m) {
        return 0;
    }
    a = parseInt(m[1], 10);
    b = parseInt(m[2], 10);
    if (a == 10 || a == 127 || a == 0) {
        return 1;
    }
    if ((a == 192 && b == 168) || (a == 169 && b == 254)) {
        return 1;
    }
    if (a == 172 && b >= 16 && b <= 31) {
        return 1;
    }
    return 0;
}

function is_tracker(name)
{
    var n;
    var i;

    n = name.toLowerCase();
    for (i = 0; i < STRIP_PREFIX.length; i++) {
        if (n.indexOf(STRIP_PREFIX[i]) == 0) {
            return 1;
        }
    }
    for (i = 0; i < STRIP_NAME.length; i++) {
        if (n == STRIP_NAME[i]) {
            return 1;
        }
    }
    return 0;
}

function skip_redirect(u)
{
    var i;
    var f;
    var t;

    for (i = 0; i < REDIRECTS.length; i++) {
        f = REDIRECTS[i].split(" ");
        if (u.hostname == f[0] && u.pathname.indexOf(f[1]) == 0) {
            t = u.searchParams.get(f[2]);
            if (t && (t.indexOf("http://") == 0 ||
                t.indexOf("https://") == 0))
            {
                return t;
            }
        }
    }
    return "";
}

function clean_url(url)
{
    var u;
    var t;
    var keys;
    var i;

    if (url.indexOf("http") != 0) {
        return {};
    }
    u = new URL(url);
    if (state.skip) {
        t = skip_redirect(u);
        if (t != "") {
            u = new URL(t);
        }
    }
    if (state.strip) {
        keys = [];
        u.searchParams.forEach(function (v, k) {
            keys.push(k);
        });
        for (i = 0; i < keys.length; i++) {
            if (is_tracker(keys[i])) {
                u.searchParams.delete(keys[i]);
            }
        }
    }
    if (u.toString() == url) {
        return {};
    }
    return { redirectUrl: u.toString() };
}

function profile(os)
{
    var rv;

    if (!fx_major || os == "default") {
        return null;
    }
    rv = fx_major + ".0";
    if (os == "mac") {
        return {
            ua: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:" + rv +
                ") Gecko/20100101 Firefox/" + rv,
            platform: "MacIntel",
            oscpu: "Intel Mac OS X 10.15",
            appversion: "5.0 (Macintosh)"
        };
    }
    if (os == "win") {
        return {
            ua: "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:" + rv +
                ") Gecko/20100101 Firefox/" + rv,
            platform: "Win32",
            oscpu: "Windows NT 10.0; Win64; x64",
            appversion: "5.0 (Windows)"
        };
    }
    return {
        ua: "Mozilla/5.0 (X11; Linux x86_64; rv:" + rv +
            ") Gecko/20100101 Firefox/" + rv,
        platform: "Linux x86_64",
        oscpu: "Linux x86_64",
        appversion: "5.0 (X11)"
    };
}

function base_cfg()
{
    var p;

    p = profile(state.ua);
    return {
        ua: p ? 1 : 0,
        ua_str: p ? p.ua : "",
        ua_platform: p ? p.platform : "",
        ua_oscpu: p ? p.oscpu : "",
        ua_appversion: p ? p.appversion : "",
        lang: state.lang,
        screen: state.screen,
        hw: state.hw,
        canvas: state.canvas,
        audio: state.audio,
        ref: state.ref,
        rfp: state.rfp,
        geo: state.geo,
        geo_lat: parseFloat(state.geo_lat) || 0,
        geo_lon: parseFloat(state.geo_lon) || 0
    };
}

function page_cfg()
{
    var cfg;
    var oses;
    var q;
    var i;

    cfg = base_cfg();
    cfg.fp = state.fp;
    cfg.secret = secret;
    cfg.faces = face_map();
    cfg.gen = state.gen;
    cfg.site_gen = site_gen;
    cfg.fx = fx_major;
    cfg.os = real_os;
    oses = ["win", "mac", "linux"];
    for (i = 0; i < oses.length; i++) {
        q = profile(oses[i]);
        cfg[oses[i] + "_ua"] = q ? q.ua : "";
        cfg[oses[i] + "_platform"] = q ? q.platform : "";
        cfg[oses[i] + "_oscpu"] = q ? q.oscpu : "";
        cfg[oses[i] + "_appversion"] = q ? q.appversion : "";
    }
    return cfg;
}

function exempt_patterns()
{
    var a;
    var out;
    var i;

    a = lines_of(state.exempt);
    out = [];
    for (i = 0; i < a.length; i++) {
        if (/^[A-Za-z0-9.-]+$/.test(a[i])) {
            out.push("*://" + a[i] + "/*");
            out.push("*://*." + a[i] + "/*");
        }
    }
    return out;
}

/*
 * The new registration is made before the old one goes, so no page
 * ever loads without the lies in between; inject.js runs only once
 * per frame when both are there for a moment.
 */
function register_script()
{
    var old;
    var opts;
    var ex;
    var p;

    old = reg;
    opts = {
        matches: ["<all_urls>"],
        js: [
            { code: "var switch_cfg = " + JSON.stringify(page_cfg()) + ";" },
            { file: "face.js" },
            { file: "inject.js" }
        ],
        runAt: "document_start",
        allFrames: true,
        matchAboutBlank: true
    };
    ex = exempt_patterns();
    if (ex.length > 0) {
        opts.excludeMatches = ex;
    }
    p = browser.contentScripts.register(opts);
    reg = p;
    p.then(function () {
        if (reg == p) {
            script_ok = 1;
        }
        drop_script(old);
    }, function () {
        if (reg == p) {
            script_ok = 0;
        }
    });
}

function drop_script(old)
{
    if (!old) {
        return;
    }
    old.then(function (r) {
        r.unregister();
    }).catch(noop);
}

function set_pref(obj, value)
{
    if (!obj) {
        return;
    }
    obj.set({ value: value }).catch(function () {});
}

function check_proxy()
{
    browser.proxy.settings.get({}).then(function (r) {
        proxy_ok = r.levelOfControl == "controlled_by_this_extension" ?
            1 : 0;
    }).catch(function () {
        proxy_ok = 0;
    });
}

/*
 * Any other enabled extension with the proxy permission can reroute
 * our requests in its own proxy.onRequest listener, Tor or not.
 */
function check_rivals()
{
    browser.management.getAll().then(function (list) {
        var names;
        var i;
        var e;

        names = [];
        for (i = 0; i < list.length; i++) {
            e = list[i];
            if (e.enabled && e.id != browser.runtime.id &&
                e.permissions && e.permissions.indexOf("proxy") >= 0)
            {
                names.push(e.name);
            }
        }
        rivals = names.join(", ");
    }).catch(function () {
        rivals = "";
    });
}

function apply_proxy()
{
    var v;

    if (!browser.proxy.settings) {
        proxy_ok = 0;
        return;
    }
    if (state.tor) {
        v = {
            proxyType: "manual",
            socks: TOR_HOST + ":" + TOR_PORT,
            socksVersion: 5,
            proxyDNS: true,
            passthrough: "localhost, 127.0.0.1, [::1]"
        };
    } else {
        v = { proxyType: "none" };
    }
    browser.proxy.settings.set({ value: v }).then(check_proxy,
        function () {
            proxy_ok = 0;
        });
}

function apply_state()
{
    var text;
    var rtc_off;

    text = "";
    if (state.tor) {
        text = "T";
    }
    if (!state.js) {
        text = text + "!";
    }
    browser.browserAction.setBadgeText({ text: text });
    browser.browserAction.setBadgeBackgroundColor({ color: "#00b3c7" });
    apply_proxy();
    rtc_off = (state.webrtc || state.tor) ? true : false;
    set_pref(browser.privacy.network.peerConnectionEnabled, !rtc_off);
    set_pref(browser.privacy.network.webRTCIPHandlingPolicy,
        rtc_off ? "disable_non_proxied_udp" : "default");
    set_pref(browser.privacy.network.networkPredictionEnabled,
        state.predict ? false : true);
    set_pref(browser.privacy.websites.hyperlinkAuditingEnabled,
        state.audit ? false : true);
    set_pref(browser.privacy.websites.resistFingerprinting,
        state.rfp ? true : false);
    set_pref(browser.privacy.websites.firstPartyIsolate,
        state.fpi ? true : false);
    set_pref(browser.privacy.websites.cookieConfig, {
        behavior: state.cookies ? "reject_third_party" : "allow_all",
        nonPersistentCookies: false
    });
    apply_alarm();
}

function minutes()
{
    return parseInt(state.cache_min, 10) || 0;
}

function apply_alarm()
{
    browser.alarms.clear("tidy").then(function () {
        browser.alarms.create("tidy", {
            periodInMinutes: minutes() > 0 ? minutes() : 60
        });
    });
}

function on_alarm(a)
{
    if (a.name != "tidy") {
        return;
    }
    if (minutes() > 0) {
        browser.browsingData.remove({}, { cache: true }).catch(noop);
    }
    prune_history().catch(noop);
}

function save_state()
{
    browser.storage.local.set(snapshot());
    apply_state();
    register_script();
}

function store_cookies(id)
{
    return browser.cookies.getAll({
        storeId: id, firstPartyDomain: null, partitionKey: {}
    }).catch(function () {
        return browser.cookies.getAll({ storeId: id, firstPartyDomain: null });
    }).catch(function () {
        return browser.cookies.getAll({ storeId: id });
    }).catch(empty);
}

/* Every cookie store: the normal one, private windows, containers */
function all_cookies()
{
    return browser.cookies.getAllCookieStores().then(function (stores) {
        var a;
        var i;

        a = [];
        for (i = 0; i < stores.length; i++) {
            a.push(store_cookies(stores[i].id));
        }
        return Promise.all(a);
    }).then(function (lists) {
        var out;
        var i;

        out = [];
        for (i = 0; i < lists.length; i++) {
            out = out.concat(lists[i]);
        }
        return out;
    }).catch(function () {
        return store_cookies(undefined);
    });
}

function cookie_host(c)
{
    return c.domain.replace(/^\./, "");
}

/*
 * Is a host bound up with a kept site: the site itself, a subdomain of
 * it, or a parent domain whose cookies it is sent (".example.org" for
 * a kept www.example.org)?
 */
function keep_related(host)
{
    var a;
    var i;

    if (host == "") {
        return 0;
    }
    a = lines_of(state.keep);
    for (i = 0; i < a.length; i++) {
        if (host_match(host, a[i]) || host_match(a[i], host)) {
            return 1;
        }
    }
    return 0;
}

/* The site whose jar a cookie is in, when the jars are split */
function cookie_jar(c)
{
    if (c.firstPartyDomain) {
        return c.firstPartyDomain;
    }
    if (c.partitionKey && c.partitionKey.topLevelSite) {
        return hostname(c.partitionKey.topLevelSite);
    }
    return "";
}

/*
 * A cookie in another site's jar is kept only when that site is kept:
 * a kept site's cookies set while it was embedded in some other site
 * are tracking, not your login there.
 */
function cookie_kept(c)
{
    var jar;

    jar = cookie_jar(c);
    if (jar != "") {
        return keep_related(jar);
    }
    return keep_related(cookie_host(c));
}

function remove_cookie(c)
{
    var d;

    d = {
        url: (c.secure ? "https://" : "http://") + cookie_host(c) + c.path,
        name: c.name,
        storeId: c.storeId
    };
    if (c.firstPartyDomain !== undefined) {
        d.firstPartyDomain = c.firstPartyDomain;
    }
    if (c.partitionKey) {
        d.partitionKey = c.partitionKey;
    }
    browser.cookies.remove(d).catch(function () {});
}

function remove_storage(hosts)
{
    var types;
    var i;
    var o;

    if (hosts.length == 0) {
        return;
    }
    types = ["localStorage", "indexedDB", "serviceWorkers"];
    for (i = 0; i < types.length; i++) {
        o = {};
        o[types[i]] = true;
        browser.browsingData.remove({ hostnames: hosts }, o)
            .catch(function () {});
    }
}

function wipe_now()
{
    ip_cache = {};
    http_once = {};
    secret = make_secret();
    site_gen = {};
    reset_faces();
    register_script();
    forget_closed("");
    return browser.browsingData.remove({}, {
        cookies: true,
        localStorage: true,
        indexedDB: true,
        serviceWorkers: true,
        cache: true,
        history: true,
        downloads: true,
        formData: true
    }).catch(noop);
}

function wipe_unkept()
{
    return all_cookies().then(function (cookies) {
        var hosts;
        var list;
        var kept;
        var seen;
        var i;
        var h;

        hosts = {};
        for (i = 0; i < cookies.length; i++) {
            if (!cookie_kept(cookies[i])) {
                remove_cookie(cookies[i]);
                /* storage is removed by host and its subdomains */
                if (!keep_related(cookie_host(cookies[i]))) {
                    hosts[cookie_host(cookies[i])] = 1;
                }
            }
        }
        seen = lines_of(state.seen);
        kept = [];
        for (i = 0; i < seen.length; i++) {
            if (keep_related(seen[i])) {
                kept.push(seen[i]);
            } else {
                hosts[seen[i]] = 1;
            }
        }
        list = [];
        for (h in hosts) {
            list.push(h);
        }
        remove_storage(list);
        state.seen = kept.join("\n");
        browser.storage.local.set({ seen: state.seen });
        ip_cache = {};
        http_once = {};
        forget_faces();
        wipe_extras();
        return wipe_history();
    }).catch(noop);
}

function all_history()
{
    return browser.history.search({
        text: "", startTime: 0, maxResults: 100000
    });
}

function prune_history()
{
    var days;

    days = parseInt(state.hist_days, 10) || 0;
    if (state.hist == "off" || days <= 0) {
        return Promise.resolve();
    }
    return browser.history.deleteRange({
        startTime: 0, endTime: Date.now() - days * 86400000
    });
}

function wipe_history()
{
    if (state.hist == "off") {
        return Promise.resolve();
    }
    if (state.hist == "all") {
        return browser.history.deleteAll();
    }
    return all_history().then(function (items) {
        var i;

        for (i = 0; i < items.length; i++) {
            if (!list_has(state.keep, hostname(items[i].url))) {
                browser.history.deleteUrl({ url: items[i].url })
                    .catch(noop);
            }
        }
        return prune_history();
    });
}

function forget_closed(host)
{
    browser.sessions.getRecentlyClosed({}).then(function (list) {
        var i;
        var s;

        for (i = 0; i < list.length; i++) {
            s = list[i];
            if (s.tab && (host == "" ||
                host_match(hostname(s.tab.url || ""), host)))
            {
                browser.sessions.forgetClosedTab(s.tab.windowId,
                    s.tab.sessionId).catch(noop);
            }
            if (s.window && host == "") {
                browser.sessions.forgetClosedWindow(s.window.sessionId)
                    .catch(noop);
            }
        }
    }).catch(noop);
}

function wipe_extras()
{
    var o;

    o = { cache: true };
    if (state.forms) {
        o.formData = true;
    }
    browser.browsingData.remove({}, o).catch(noop);
    if (state.dl) {
        browser.downloads.erase({}).catch(noop);
    }
    if (state.closed) {
        forget_closed("");
    }
}

function clear_site_history(host)
{
    return all_history().then(function (items) {
        var i;

        for (i = 0; i < items.length; i++) {
            if (host_match(hostname(items[i].url), host)) {
                browser.history.deleteUrl({ url: items[i].url })
                    .catch(noop);
            }
        }
    }).catch(noop);
}

function forget_site(host)
{
    return all_cookies().then(function (cookies) {
        var i;
        var c;

        for (i = 0; i < cookies.length; i++) {
            c = cookies[i];
            if (host_match(cookie_host(c), host) ||
                c.firstPartyDomain == host)
            {
                remove_cookie(c);
            }
        }
        remove_storage([host]);
        edit_list("seen", host, 0);
        browser.storage.local.set({ seen: state.seen });
        delete ip_cache[host];
        forget_closed(host);
        return browser.downloads.search({});
    }).then(function (list) {
        var i;

        for (i = 0; i < list.length; i++) {
            if (host_match(hostname(list[i].url), host) ||
                host_match(hostname(list[i].referrer || ""), host))
            {
                browser.downloads.erase({ id: list[i].id }).catch(noop);
            }
        }
        return clear_site_history(host);
    }).catch(noop);
}

function check_forget(host)
{
    if (!state.forget || host == "" || list_has(state.keep, host)) {
        return;
    }
    browser.tabs.query({}).then(function (tabs) {
        var i;

        for (i = 0; i < tabs.length; i++) {
            if (hostname(tabs[i].url || "") == host) {
                return;
            }
        }
        forget_site(host);
    });
}

function new_identity()
{
    secret = make_secret();
    site_gen = {};
    state.gen++;
    reset_faces();
    save_state();
    wipe_now().then(function () {
        return browser.tabs.query({});
    }).then(function (tabs) {
        var i;

        for (i = 0; i < tabs.length; i++) {
            browser.tabs.reload(tabs[i].id);
        }
    });
}

function first_party(d)
{
    var a;

    if (d.type == "main_frame") {
        return hostname(d.url);
    }
    a = d.frameAncestors;
    if (a && a.length > 0) {
        return hostname(a[a.length - 1].url);
    }
    return hostname(d.documentUrl || d.originUrl || d.url);
}

function circuit_user(site, tab)
{
    var k;
    var user;

    if (state.fp == "profile") {
        k = site_key(site);
        return "p" + hash(k).toString(16) + hash(k + "|x").toString(16);
    }
    user = "g" + state.gen;
    if (state.iso != "site") {
        user = user + "t" + tab;
    }
    if (state.iso != "tab") {
        user = user + "s" + site + "." + (site_gen[site] || 0);
    }
    return user.substring(0, 255);
}

function circuit_id(site)
{
    if (state.fp != "profile" && state.iso != "site") {
        return "per tab";
    }
    return hash(circuit_user(site, -1)).toString(16);
}

/* Onions have no exit, and local addresses never go through Tor */
function no_exit(host)
{
    return is_private(host) || host.endsWith(".onion") ? 1 : 0;
}

function lookup_ip(host)
{
    var n;

    if (!state.tor) {
        return Promise.resolve({ ip: "", note: "Tor is off" });
    }
    if (no_exit(host)) {
        return Promise.resolve({ ip: "", note: "no exit for this site" });
    }
    if (state.fp != "profile" && state.iso != "site") {
        return Promise.resolve({ ip: "", note: "differs per tab" });
    }
    n = make_secret().replace(/-/g, "");
    ip_wait[n] = host;
    return fetch(IP_URL + "?n=" + n, {
        credentials: "omit", cache: "no-store"
    }).then(function (r) {
        return r.json();
    }).then(function (j) {
        delete ip_wait[n];
        ip_cache[host] = { ip: String(j.IP), time: Date.now() };
        if (j.IsTor) {
            return { ip: String(j.IP), note: "" };
        }
        if (rivals != "") {
            return { ip: String(j.IP), note: "Not Tor: " + rivals +
                " took the connection." };
        }
        return { ip: String(j.IP), note: "Not Tor!" };
    }).catch(function () {
        delete ip_wait[n];
        return { ip: "", note: "lookup failed" };
    });
}

function site_info(host)
{
    var f;
    var ip;

    f = face_of(site_key(host), real_os);
    ip = ip_cache[host];
    return {
        host: host,
        fp: state.fp,
        tor: state.tor,
        face: state.fp == "profile" ? face_text(f, state.rfp) : "uniform",
        os: f.os,
        circuit: circuit_id(host),
        ip: ip ? ip.ip : "",
        ip_time: ip ? ip.time : 0,
        kept: list_has(state.keep, host),
        js: (state.js || list_has(state.sites, host)) ? 1 : 0,
        exempt: list_has(state.exempt, host),
        cookies: 0
    };
}

function site_rows()
{
    return Promise.all([
        all_cookies(),
        all_history().catch(empty)
    ]).then(function (res) {
        var cookies;
        var items;
        var hosts;
        var count;
        var hcount;
        var lists;
        var rows;
        var a;
        var i;
        var j;
        var h;
        var r;

        cookies = res[0];
        items = res[1];
        hosts = {};
        count = {};
        hcount = {};
        for (i = 0; i < items.length; i++) {
            h = hostname(items[i].url);
            if (h != "") {
                hosts[h] = 1;
                hcount[h] = (hcount[h] || 0) + 1;
            }
        }
        lists = [state.seen, state.keep, state.sites, state.exempt];
        for (i = 0; i < lists.length; i++) {
            a = lines_of(lists[i]);
            for (j = 0; j < a.length; j++) {
                hosts[a[j]] = 1;
            }
        }
        for (i = 0; i < cookies.length; i++) {
            h = cookie_host(cookies[i]);
            hosts[h] = 1;
            count[h] = (count[h] || 0) + 1;
        }
        rows = [];
        for (h in hosts) {
            r = site_info(h);
            r.cookies = count[h] || 0;
            r.history = hcount[h] || 0;
            rows.push(r);
        }
        rows.sort(function (x, y) {
            return x.host < y.host ? -1 : (x.host > y.host ? 1 : 0);
        });
        return { rows: rows, tor: state.tor, fp: state.fp };
    });
}

function cookie_list(host)
{
    return all_cookies().then(function (cookies) {
        var out;
        var i;
        var c;
        var h;

        out = [];
        for (i = 0; i < cookies.length; i++) {
            c = cookies[i];
            h = cookie_host(c);
            if (host_match(h, host) || c.firstPartyDomain == host) {
                out.push({
                    name: c.name,
                    value: c.value,
                    domain: c.domain,
                    path: c.path,
                    expires: c.session ? 0 : c.expirationDate,
                    secure: c.secure ? 1 : 0,
                    http_only: c.httpOnly ? 1 : 0,
                    first_party: c.firstPartyDomain || ""
                });
            }
        }
        return out;
    });
}

function flat_bookmarks(node, out)
{
    var i;

    if (node.url) {
        out.push({ title: node.title || "", url: node.url });
    }
    if (node.children) {
        for (i = 0; i < node.children.length; i++) {
            flat_bookmarks(node.children[i], out);
        }
    }
    return out;
}

function tab_entry(t)
{
    return { title: t.title || "", url: t.url || "" };
}

function site_detail(host)
{
    return Promise.all([
        cookie_list(host).catch(empty),
        all_history().catch(empty),
        browser.downloads.search({}).catch(empty),
        browser.bookmarks.getTree().catch(empty),
        browser.tabs.query({}).catch(empty),
        browser.sessions.getRecentlyClosed({}).catch(empty)
    ]).then(function (r) {
        var d;
        var i;
        var j;
        var x;
        var marks;

        d = {
            cookies: r[0], history: [], downloads: [], bookmarks: [],
            tabs: [], closed: []
        };
        for (i = 0; i < r[1].length; i++) {
            x = r[1][i];
            if (host_match(hostname(x.url), host)) {
                d.history.push({
                    url: x.url, title: x.title || "",
                    last: x.lastVisitTime || 0, visits: x.visitCount || 0
                });
            }
        }
        d.history.sort(function (a, b) {
            return b.last - a.last;
        });
        for (i = 0; i < r[2].length; i++) {
            x = r[2][i];
            if (host_match(hostname(x.url), host) ||
                host_match(hostname(x.referrer || ""), host))
            {
                d.downloads.push({
                    file: x.filename, url: x.url,
                    time: x.startTime, state: x.state
                });
            }
        }
        marks = [];
        for (i = 0; i < r[3].length; i++) {
            flat_bookmarks(r[3][i], marks);
        }
        for (i = 0; i < marks.length; i++) {
            if (host_match(hostname(marks[i].url), host)) {
                d.bookmarks.push(marks[i]);
            }
        }
        for (i = 0; i < r[4].length; i++) {
            if (host_match(hostname(r[4][i].url || ""), host)) {
                d.tabs.push(tab_entry(r[4][i]));
            }
        }
        for (i = 0; i < r[5].length; i++) {
            x = r[5][i];
            if (x.tab && host_match(hostname(x.tab.url || ""), host)) {
                d.closed.push(tab_entry(x.tab));
            }
            if (x.window && x.window.tabs) {
                for (j = 0; j < x.window.tabs.length; j++) {
                    if (host_match(hostname(x.window.tabs[j].url || ""),
                        host))
                    {
                        d.closed.push(tab_entry(x.window.tabs[j]));
                    }
                }
            }
        }
        return d;
    });
}

function read_storage(host)
{
    return browser.tabs.query({}).then(function (tabs) {
        var i;

        for (i = 0; i < tabs.length; i++) {
            if (hostname(tabs[i].url || "") == host) {
                return browser.tabs.executeScript(tabs[i].id, {
                    code: STORAGE_CODE
                }).then(function (r) {
                    r[0].found = 1;
                    return r[0];
                });
            }
        }
        return { found: 0 };
    }).catch(function () {
        return { found: 0, failed: 1 };
    });
}

/*
 * Should on_request ever throw, IceCat would fall back to the prefs'
 * proxy; this one answers Tor even then, on a circuit of its own.
 */
function on_request(d)
{
    try {
        return route(d);
    } catch (e) {
        if (state.loaded && !state.tor) {
            return { type: "direct" };
        }
        return {
            type: "socks", host: TOR_HOST, port: TOR_PORT,
            username: "error", password: "x", proxyDNS: true
        };
    }
}

function route(d)
{
    var site;
    var n;

    if (state.loaded && !state.tor) {
        return { type: "direct" };
    }
    if (is_private(hostname(d.url))) {
        return { type: "direct" };
    }
    site = first_party(d);
    if (d.url.indexOf(IP_URL) == 0) {
        n = new URL(d.url).searchParams.get("n");
        if (n && ip_wait[n] !== undefined) {
            site = ip_wait[n];
        }
    }
    return {
        type: "socks",
        host: TOR_HOST,
        port: TOR_PORT,
        username: circuit_user(site, d.tabId),
        password: "x",
        proxyDNS: true
    };
}

/* Hosts that need no HTTPS: Tor encrypts onions, LAN is yours */
function http_ok(host)
{
    if (host == "" || is_private(host) || host.endsWith(".onion")) {
        return 1;
    }
    if (http_once[host] || list_has(state.http_ok, host)) {
        return 1;
    }
    return 0;
}

/*
 * HTTPS only: plain http:// is turned into https://. A page that
 * cannot be had over HTTPS goes to nohttps.html, where you choose.
 */
function upgrade(d)
{
    var u;
    var h;
    var n;

    if (!state.https) {
        return null;
    }
    if (d.url.indexOf("http://") != 0 && d.url.indexOf("ws://") != 0) {
        return null;
    }
    u = new URL(d.url);
    h = u.hostname;
    if (http_ok(h)) {
        return null;
    }
    if (d.type == "main_frame") {
        n = (http_tries[d.tabId + " " + h] || 0) + 1;
        http_tries[d.tabId + " " + h] = n;
        if (n > 3) {
            /*
             * The site keeps sending us back to plain HTTP. The tab is
             * sent to the page rather than redirected, which would need
             * the page to be open to every site (web_accessible).
             */
            delete http_tries[d.tabId + " " + h];
            if (d.tabId >= 0) {
                browser.tabs.update(d.tabId, {
                    url: nohttps_url(d.url, "loop")
                }).catch(noop);
            }
            return { cancel: true };
        }
        http_wait[d.tabId] = d.url;
    }
    if (u.protocol == "ws:") {
        u.protocol = "wss:";
    } else {
        u.protocol = "https:";
    }
    if (u.port == "80") {
        u.port = "";
    }
    return { redirectUrl: u.href };
}

function nohttps_url(url, why)
{
    return browser.runtime.getURL("nohttps.html") + "?why=" + why +
        "&u=" + encodeURIComponent(url);
}

function on_error(d)
{
    var orig;

    if (d.type != "main_frame" || !http_wait[d.tabId]) {
        return;
    }
    orig = http_wait[d.tabId];
    delete http_wait[d.tabId];
    delete http_tries[d.tabId + " " + hostname(orig)];
    if (d.error == "NS_BINDING_ABORTED" ||
        d.url.indexOf("https://") != 0 ||
        hostname(d.url) != hostname(orig))
    {
        return;
    }
    browser.tabs.update(d.tabId, { url: nohttps_url(orig, "fail") });
}

function on_done(d)
{
    if (d.type == "main_frame") {
        delete http_wait[d.tabId];
        delete http_tries[d.tabId + " " + hostname(d.url)];
    }
}

function on_before(d)
{
    var t;
    var r;

    t = d.type;
    if (t == "sub_frame" && d.url.indexOf("http") == 0) {
        /* embedded sites keep data too; wipe_unkept must know them */
        note_seen(hostname(d.url));
    }
    r = upgrade(d);
    if (r) {
        return r;
    }
    if (state.beacon &&
        (t == "beacon" || t == "csp_report" || t == "speculative"))
    {
        return { cancel: true };
    }
    if (state.fonts && t == "font") {
        return { cancel: true };
    }
    if (state.lan && t != "main_frame" && d.originUrl &&
        d.originUrl.indexOf("http") == 0 &&
        is_private(hostname(d.url)) &&
        !is_private(hostname(d.originUrl)))
    {
        return { cancel: true };
    }
    if (t == "main_frame" && d.method == "GET") {
        try {
            return clean_url(d.url);
        } catch (e) {
            return {};
        }
    }
    return {};
}

function on_send(d)
{
    var h;
    var kept;
    var p;
    var i;
    var name;

    h = d.requestHeaders;
    if (state.fp == "profile") {
        p = profile(real_os);
    } else {
        p = profile(state.ua);
    }
    kept = [];
    for (i = 0; i < h.length; i++) {
        name = h[i].name.toLowerCase();
        if (name == "user-agent" && p) {
            h[i].value = p.ua;
        }
        if (name == "accept-language" && state.lang) {
            h[i].value = LANG_VALUE;
        }
        if (name == "referer" && state.ref &&
            hostname(h[i].value) != hostname(d.url))
        {
            continue;
        }
        if (name == "if-none-match" && state.etag) {
            continue;
        }
        kept.push(h[i]);
    }
    return { requestHeaders: kept };
}

function on_headers(d)
{
    var h;
    var out;
    var i;
    var frame;
    var run;

    h = d.responseHeaders;
    out = [];
    for (i = 0; i < h.length; i++) {
        if (state.etag && h[i].name.toLowerCase() == "etag") {
            continue;
        }
        out.push(h[i]);
    }
    frame = (d.type == "main_frame" || d.type == "sub_frame") ? 1 : 0;
    run = state.loaded && script_ok && (state.js ||
        list_has(state.sites, hostname(d.url)));
    if (frame && !run) {
        out.push({ name: "Content-Security-Policy", value: CSP_VALUE });
    }
    return { responseHeaders: out };
}

/* Only switch's own pages may give orders, never a page's frame */
function from_us(sender)
{
    var base;

    base = browser.runtime.getURL("");
    return sender && sender.id == browser.runtime.id && !sender.frameId &&
        String(sender.url || "").indexOf(base) == 0 ? 1 : 0;
}

function on_message(msg, sender)
{
    var old_tor;

    if (!from_us(sender) || !msg || typeof msg.cmd != "string") {
        return Promise.reject(new Error("not from switch"));
    }
    if (msg.cmd == "sites") {
        return site_rows();
    }
    if (msg.cmd == "info") {
        return Promise.resolve(site_info(msg.host));
    }
    if (msg.cmd == "cookies") {
        return cookie_list(msg.host);
    }
    if (msg.cmd == "ip") {
        return lookup_ip(msg.host);
    }
    if (msg.cmd == "forget") {
        return forget_site(msg.host);
    }
    if (msg.cmd == "wipe_unkept") {
        return wipe_unkept();
    }
    if (msg.cmd == "detail") {
        return site_detail(msg.host);
    }
    if (msg.cmd == "storage") {
        return read_storage(msg.host);
    }
    if (msg.cmd == "clear_history") {
        return clear_site_history(msg.host);
    }
    old_tor = state.tor;
    if (msg.cmd == "set" && DEF.hasOwnProperty(msg.key) &&
        msg.key != "gen" &&
        msg.key != "seen" && msg.key != "faces")
    {
        if (typeof DEF[msg.key] == "string") {
            state[msg.key] = String(msg.value);
        } else {
            state[msg.key] = msg.value ? 1 : 0;
        }
    }
    if (msg.cmd == "site") {
        edit_list("sites", msg.host, msg.allow);
    }
    if (msg.cmd == "keep") {
        edit_list("keep", msg.host, msg.value);
    }
    if (msg.cmd == "http" && msg.host) {
        edit_list("http_ok", msg.host, msg.value);
        if (!msg.value) {
            delete http_once[msg.host];
        }
    }
    if (msg.cmd == "http_once" && msg.host) {
        http_once[msg.host] = 1;
    }
    if (msg.cmd == "newid") {
        state.gen++;
        ip_cache = {};
    }
    if (msg.cmd == "wipe") {
        wipe_now();
    }
    if (msg.cmd == "ident") {
        new_identity();
    }
    if (msg.cmd == "face" && msg.host) {
        site_gen[msg.host] = (site_gen[msg.host] || 0) + 1;
        delete ip_cache[msg.host];
        register_script();
    }
    if (old_tor != state.tor && state.wipe_switch) {
        state.gen++;
        wipe_now();
    }
    if (msg.cmd == "set" || msg.cmd == "site" || msg.cmd == "keep" ||
        msg.cmd == "newid" || msg.cmd == "http")
    {
        save_state();
    }
    if (msg.cmd == "keep" || (msg.cmd == "set" && msg.key == "keep")) {
        sync_faces();
    }
    return Promise.resolve(reply());
}

function reply()
{
    var r;

    r = snapshot();
    r.proxy_ok = proxy_ok;
    r.rivals = rivals;
    r.script_ok = script_ok;
    return r;
}

function note_seen(host)
{
    if (host == "" || lines_of(state.seen).indexOf(host) >= 0) {
        return;
    }
    edit_list("seen", host, 1);
    browser.storage.local.set({ seen: state.seen });
}

function on_tab_updated(id, change, tab)
{
    var old;
    var host;

    if (!tab.url) {
        return;
    }
    host = "";
    if (tab.url.indexOf("http") == 0) {
        host = hostname(tab.url);
    }
    note_seen(host);
    if (change.status == "complete") {
        auto_ip(host);
    }
    old = tab_host[id];
    tab_host[id] = host;
    if (old && old != host) {
        check_forget(old);
    }
}

function on_tab_removed(id)
{
    var host;

    host = tab_host[id];
    delete tab_host[id];
    if (host) {
        check_forget(host);
    }
}

function on_window_removed()
{
    if (!state.wipe) {
        return;
    }
    browser.windows.getAll().then(function (w) {
        if (w.length == 0) {
            wipe_unkept();
        }
    });
}

function on_startup()
{
    /* The wipe already runs at every start, from on_loaded */
    if (!state.loaded) {
        want_wipe = 1;
    }
}

function on_loaded(r)
{
    var k;

    for (k in DEF) {
        if (r[k] !== undefined) {
            state[k] = r[k];
        }
    }
    state.loaded = 1;
    apply_state();
    sync_faces();
    register_script();
    if (want_wipe && state.wipe) {
        want_wipe = 0;
        wipe_unkept();
    }
    Promise.all([
        browser.runtime.getBrowserInfo(),
        browser.runtime.getPlatformInfo()
    ]).then(function (r) {
        fx_major = parseInt(r[0].version, 10);
        real_os = r[1].os;
        register_script();
    }).catch(noop);
}

init_state();
browser.proxy.onRequest.addListener(on_request, { urls: ["<all_urls>"] });
browser.webRequest.onBeforeRequest.addListener(
    on_before,
    { urls: ["<all_urls>"] },
    ["blocking"]
);
browser.webRequest.onErrorOccurred.addListener(on_error,
    { urls: ["https://*/*"], types: ["main_frame"] });
browser.webRequest.onCompleted.addListener(on_done,
    { urls: ["<all_urls>"], types: ["main_frame"] });
browser.webRequest.onBeforeSendHeaders.addListener(
    on_send,
    { urls: ["<all_urls>"] },
    ["blocking", "requestHeaders"]
);
browser.webRequest.onHeadersReceived.addListener(
    on_headers,
    { urls: ["<all_urls>"] },
    ["blocking", "responseHeaders"]
);
browser.runtime.onMessage.addListener(on_message);
browser.runtime.onStartup.addListener(on_startup);
browser.tabs.onUpdated.addListener(on_tab_updated);
browser.tabs.onRemoved.addListener(on_tab_removed);
browser.windows.onRemoved.addListener(on_window_removed);
browser.alarms.onAlarm.addListener(on_alarm);
browser.management.onEnabled.addListener(check_rivals);
browser.management.onDisabled.addListener(check_rivals);
browser.management.onInstalled.addListener(check_rivals);
browser.management.onUninstalled.addListener(check_rivals);
check_rivals();
browser.storage.local.get(Object.keys(DEF)).then(on_loaded);
