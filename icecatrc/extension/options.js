var saved_timer = 0;

var KEYS = [
    "tor", "no_tor", "https", "http_ok", "cookie_all", "cookie_sites",
    "auto_ip", "iso", "wipe_switch", "webrtc", "lan", "js", "sites",
    "fp", "rfp", "ua", "lang", "hw", "canvas", "audio", "screen", "geo",
    "geo_lat", "geo_lon", "exempt", "ref", "strip", "skip", "beacon",
    "etag", "fonts", "cookies", "fpi", "audit", "predict", "forget",
    "keep", "nokeep_cookies", "nokeep_storage", "nokeep_history",
    "nokeep_settings", "nokeep_face", "wipe", "hist", "hist_days",
    "cache_min", "dl", "forms", "closed"
];

function show(s)
{
    var i;
    var el;

    for (i = 0; i < KEYS.length; i++) {
        el = document.getElementById(KEYS[i]);
        if (el.type == "checkbox") {
            el.checked = s[KEYS[i]] ? true : false;
        } else {
            el.value = s[KEYS[i]];
        }
    }
}

function send(cmd, key, value)
{
    return browser.runtime.sendMessage({
        cmd: cmd, key: key, value: value
    }).then(show);
}

function on_change(e)
{
    var el;

    el = e.target;
    if (KEYS.indexOf(el.id) < 0) {
        return;
    }
    if (el.type == "checkbox") {
        send("set", el.id, el.checked).then(saved);
    } else {
        send("set", el.id, el.value).then(saved);
    }
}

function saved()
{
    var s;

    s = document.getElementById("saved");
    s.style.display = "block";
    window.clearTimeout(saved_timer);
    saved_timer = window.setTimeout(function () {
        s.style.display = "none";
    }, 1200);
}

/* 1 to 6 jump to a section, unless you are typing in a field */
function on_key(e)
{
    var t;
    var h;

    t = e.target.tagName;
    if (e.ctrlKey || e.altKey || e.metaKey || t == "TEXTAREA" ||
        (t == "INPUT" && e.target.type != "checkbox"))
    {
        return;
    }
    if (e.key >= "1" && e.key <= "6") {
        e.preventDefault();
        h = document.getElementById("s" + e.key);
        h.scrollIntoView();
        h = h.nextElementSibling.querySelector("input, select, " +
            "textarea, button");
        if (h) {
            h.focus();
        }
    }
}

function ask(text, cmd)
{
    if (window.confirm(text)) {
        send(cmd, "", 0);
    }
}

document.addEventListener("change", on_change);
document.addEventListener("keydown", on_key);
document.getElementById("showsites").addEventListener("click", function () {
    browser.tabs.create({ url: browser.runtime.getURL("sites.html") });
});
document.getElementById("newid").addEventListener("click", function () {
    send("newid", "", 0);
});
document.getElementById("ident").addEventListener("click", function () {
    ask("Wipe all site data, new circuits and reload every tab?",
        "ident");
});
document.getElementById("wipenow").addEventListener("click", function () {
    ask("Wipe all cookies, site data, history, downloads list and" +
        " form history now, kept sites included?", "wipe");
});
send("get", "", 0);
