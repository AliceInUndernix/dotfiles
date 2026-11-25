var cur = null;
var host = "";
var armed = "";
var tried = 0;

function in_list(text)
{
    var lines;
    var i;

    lines = text.split("\n");
    for (i = 0; i < lines.length; i++) {
        if (host != "" && lines[i].trim() == host) {
            return true;
        }
    }
    return false;
}

function el(id)
{
    return document.getElementById(id);
}

function say(id, text, cls)
{
    el(id).textContent = text;
    el(id).className = "desc " + (cls || "");
}

function warn(s)
{
    var w;

    w = "";
    if (!s.proxy_ok) {
        w = "The proxy is not under switch's control. Allow switch in " +
            "private windows, and remove other proxy add-ons.";
    }
    if (s.rivals) {
        w = s.rivals + " can send your traffic around Tor. Remove it " +
            "in about:addons.";
    }
    if (!s.script_ok) {
        w = "The page lies are not set up yet; until they are, no page " +
            "runs JavaScript.";
    }
    el("warn").textContent = w;
    el("warn").style.display = w == "" ? "none" : "block";
}

function show(s)
{
    var badge;

    cur = s;
    el("tor").checked = s.tor ? true : false;
    el("js").checked = s.js ? true : false;
    el("site").checked = in_list(s.sites);
    el("torsite").checked = !in_list(s.no_tor);
    el("torsite_line").style.display = host == "" || !s.tor ?
        "none" : "block";
    el("torsite_label").textContent = "Tor on " + host;
    el("http").checked = in_list(s.http_ok);
    el("site_line").style.display = host == "" || s.js ? "none" : "block";
    el("http_line").style.display = host == "" || !s.https ?
        "none" : "block";
    el("here").style.display = host == "" ? "none" : "block";
    el("site_label").textContent = "JavaScript on " + host;
    el("http_label").textContent = "Plain HTTP on " + host;
    el("host_name").textContent = host;
    if (!s.tor) {
        say("tor_text", "Off: sites see your real address.", "off");
    } else if (host != "" && in_list(s.no_tor)) {
        say("tor_text", "On, but not here: " + host +
            " goes through the VPN.", "off");
    } else if (s.fp == "profile") {
        say("tor_text", "On: each site has its own IP and profile.", "on");
    } else {
        say("tor_text", "On: a circuit per " + s.iso + ".", "on");
    }
    badge = [];
    badge.push(s.tor ? "Tor" : "direct");
    badge.push(s.js ? "scripts on" : "scripts off");
    if (s.https) {
        badge.push("HTTPS only");
    }
    el("state").textContent = badge.join(", ");
    warn(s);
}

function show_info(r)
{
    if (host == "") {
        return;
    }
    say("face_text", "Looks like: " + r.face);
    if (r.ip == "" && cur && cur.tor && cur.auto_ip && !tried) {
        tried = 1;
        do_lookup();
        return;
    }
    if (!cur || !cur.tor) {
        say("ip_text", "Exit: none, Tor is off.");
    } else if (!r.tor) {
        say("ip_text", "Exit: none, this site goes through the VPN.");
    } else if (r.ip == "") {
        say("ip_text", "Exit: not looked up yet.");
    } else {
        say("ip_text", "Exit: " + r.ip + " (circuit " + r.circuit + ")");
    }
}

function send(cmd, key, value)
{
    return browser.runtime.sendMessage({
        cmd: cmd, key: key, value: value, host: host, allow: value
    }).then(function (s) {
        show(s);
        if (host != "") {
            browser.runtime.sendMessage({ cmd: "info", host: host })
                .then(show_info);
        }
    });
}

function do_lookup()
{
    if (host == "" || !cur || !cur.tor) {
        return;
    }
    say("ip_text", "Exit: looking up...");
    browser.runtime.sendMessage({ cmd: "ip", host: host }).then(
        function (res) {
            if (res.note != "") {
                say("ip_text", "Exit: " + res.note, "off");
            } else {
                send("get", "", 0);
            }
        });
}

function do_sites()
{
    browser.tabs.create({ url: browser.runtime.getURL("sites.html") });
    window.close();
}

function do_options()
{
    browser.runtime.openOptionsPage();
    window.close();
}

/* Dangerous buttons want a second press */
function confirm_then(id, cmd, label)
{
    var b;

    b = el(id);
    if (armed != id) {
        armed = id;
        b.firstChild.nodeValue = "Sure? Press again ";
        b.className = "danger";
        return;
    }
    armed = "";
    b.firstChild.nodeValue = label + " ";
    b.className = "";
    send(cmd, "", 0);
}

function do_face()
{
    if (host == "") {
        return;
    }
    send("face", "", 0).then(function () {
        browser.tabs.reload();
    });
}

function do_http(on)
{
    if (host == "") {
        return;
    }
    send("http", "", on).then(function () {
        browser.tabs.reload();
    });
}

function do_tor_site(on)
{
    if (host == "") {
        return;
    }
    send("tor_site", "", on).then(function () {
        browser.tabs.reload();
    });
}

function on_key(e)
{
    if (e.ctrlKey || e.altKey || e.metaKey) {
        return;
    }
    if (e.key == "t") {
        send("set", "tor", !cur.tor);
    }
    if (e.key == "j") {
        send("set", "js", !cur.js);
    }
    if (e.key == "v" && host != "" && cur.tor) {
        do_tor_site(in_list(cur.no_tor));
    }
    if (e.key == "s" && host != "" && !cur.js) {
        send("site", "", !in_list(cur.sites));
    }
    if (e.key == "h" && host != "" && cur.https) {
        do_http(!in_list(cur.http_ok));
    }
    if (e.key == "c") {
        do_face();
    }
    if (e.key == "l") {
        do_lookup();
    }
    if (e.key == "r") {
        browser.tabs.reload();
    }
    if (e.key == "n") {
        send("newid", "", 0);
    }
    if (e.key == "i") {
        confirm_then("ident", "ident", "New identity");
    }
    if (e.key == "w") {
        confirm_then("wipe", "wipe", "Wipe all");
    }
    if (e.key == "d") {
        do_sites();
    }
    if (e.key == "o") {
        do_options();
    }
}

function start(tabs)
{
    var u;

    host = "";
    if (tabs.length > 0 && tabs[0].url) {
        u = new URL(tabs[0].url);
        if (u.protocol == "http:" || u.protocol == "https:") {
            host = u.hostname;
        }
    }
    send("get", "", 0);
}

el("tor").addEventListener("change", function () {
    send("set", "tor", this.checked);
});
el("js").addEventListener("change", function () {
    send("set", "js", this.checked);
});
el("torsite").addEventListener("change", function () {
    do_tor_site(this.checked);
});
el("site").addEventListener("change", function () {
    send("site", "", this.checked);
});
el("http").addEventListener("change", function () {
    do_http(this.checked);
});
el("face").addEventListener("click", do_face);
el("lookup").addEventListener("click", do_lookup);
el("reload").addEventListener("click", function () {
    browser.tabs.reload();
});
el("newid").addEventListener("click", function () {
    send("newid", "", 0);
});
el("ident").addEventListener("click", function () {
    confirm_then("ident", "ident", "New identity");
});
el("wipe").addEventListener("click", function () {
    confirm_then("wipe", "wipe", "Wipe all");
});
el("sites").addEventListener("click", do_sites);
el("options").addEventListener("click", do_options);
document.addEventListener("keydown", on_key);
browser.tabs.query({ active: true, currentWindow: true }).then(start);
