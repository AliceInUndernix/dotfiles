var rows = [];
var shown = [];
var sel = -1;
/* the marked sites, by host: f and p act on all of them */
var marked = {};

function send(msg)
{
    return browser.runtime.sendMessage(msg);
}

function status(text)
{
    document.getElementById("status").textContent = text;
}

function load()
{
    return send({ cmd: "sites" }).then(function (r) {
        rows = r.rows;
        document.getElementById("warn").textContent =
            (r.wipe ? "" : "The wipe at start is off in Settings: " +
            "these sites stay until you forget them. ") +
            (r.forget ? "" : "Forgetting a site when its last tab " +
            "closes is off in Settings.");
        document.getElementById("warn").style.display =
            (r.wipe && r.forget) ? "none" : "block";
        render();
    });
}

function clear(el)
{
    while (el.firstChild) {
        el.removeChild(el.firstChild);
    }
}

function cell(tr, text, cls)
{
    var td;

    td = document.createElement("td");
    td.textContent = text;
    if (cls) {
        td.className = cls;
    }
    tr.appendChild(td);
}

function time_text(ms)
{
    if (!ms) {
        return "-";
    }
    return new Date(ms).toISOString().substring(0, 16).replace("T", " ");
}

function ip_text(r)
{
    if (r.ip == "") {
        return "-";
    }
    return r.ip;
}

function fill(id, list, fn)
{
    var body;
    var tr;
    var i;

    body = document.getElementById(id);
    clear(body);
    for (i = 0; i < list.length; i++) {
        tr = document.createElement("tr");
        fn(tr, list[i]);
        body.appendChild(tr);
    }
    if (list.length == 0) {
        tr = document.createElement("tr");
        cell(tr, "None.", "key");
        body.appendChild(tr);
    }
}

function make_row(r, i)
{
    var tr;

    tr = document.createElement("tr");
    tr.tabIndex = 0;
    cell(tr, marked[r.host] ? "[x]" : "[ ]",
        marked[r.host] ? "mark on" : "mark");
    tr.firstChild.addEventListener("click", function () {
        toggle_mark(i);
    });
    cell(tr, r.host, "");
    cell(tr, r.kept ? "kept " + r.parts : "", "on");
    cell(tr, r.js ? "on" : "off", r.js ? "on" : "off");
    cell(tr, String(r.cookies), "");
    cell(tr, String(r.history), "");
    cell(tr, r.face, "");
    cell(tr, r.circuit, "key");
    cell(tr, ip_text(r), "");
    tr.addEventListener("focus", function () {
        select(i);
    });
    if (i == sel) {
        tr.className = "sel";
    }
    return tr;
}

function render()
{
    var body;
    var filter;
    var i;

    body = document.getElementById("rows");
    filter = document.getElementById("filter").value.trim();
    clear(body);
    shown = [];
    for (i = 0; i < rows.length; i++) {
        if (filter == "" || rows[i].host.indexOf(filter) >= 0) {
            shown.push(rows[i]);
        }
    }
    for (i = 0; i < shown.length; i++) {
        body.appendChild(make_row(shown[i], i));
    }
    document.getElementById("count").textContent = shown.length +
        " sites, " + marked_hosts().length + " marked";
    if (sel >= shown.length) {
        sel = shown.length - 1;
    }
    details();
}

function select(i)
{
    var trs;

    trs = document.getElementById("rows").childNodes;
    if (sel >= 0 && sel < trs.length) {
        trs[sel].className = "";
    }
    sel = i;
    if (sel >= 0 && sel < trs.length) {
        trs[sel].className = "sel";
    }
    details();
}

function focus_row(i)
{
    var trs;

    trs = document.getElementById("rows").childNodes;
    if (trs.length == 0) {
        return;
    }
    if (i < 0) {
        i = 0;
    }
    if (i >= trs.length) {
        i = trs.length - 1;
    }
    trs[i].focus();
}

function current()
{
    if (sel < 0 || sel >= shown.length) {
        return null;
    }
    return shown[sel];
}

function show_detail(d)
{
    fill("d_cookies", d.cookies, function (tr, c) {
        cell(tr, c.name, "");
        cell(tr, c.value, "val");
        cell(tr, c.domain, "");
        cell(tr, c.path, "");
        cell(tr, c.expires ? time_text(c.expires * 1000) : "session", "");
        cell(tr, (c.secure ? "secure " : "") +
            (c.http_only ? "httponly" : ""), "key");
        cell(tr, c.first_party, "");
    });
    fill("d_history", d.history, function (tr, h) {
        cell(tr, time_text(h.last), "");
        cell(tr, String(h.visits), "");
        cell(tr, h.title, "");
        cell(tr, h.url, "val");
    });
    fill("d_downloads", d.downloads, function (tr, x) {
        cell(tr, x.time ? x.time.substring(0, 16).replace("T", " ") : "-",
            "");
        cell(tr, x.file, "val");
        cell(tr, x.url, "val");
        cell(tr, x.state, "");
    });
    fill("d_bookmarks", d.bookmarks, function (tr, b) {
        cell(tr, b.title, "");
        cell(tr, b.url, "val");
    });
    fill("d_tabs", d.tabs, function (tr, t) {
        cell(tr, t.title, "");
        cell(tr, t.url, "val");
    });
    fill("d_closed", d.closed, function (tr, t) {
        cell(tr, t.title, "");
        cell(tr, t.url, "val");
    });
}

function details()
{
    var box;
    var r;
    var host;

    box = document.getElementById("detail");
    r = current();
    if (!r) {
        box.style.display = "none";
        return;
    }
    host = r.host;
    box.style.display = "block";
    document.getElementById("d_host").textContent = host;
    document.getElementById("d_text").textContent =
        "Kept: " + (r.kept ? "yes, " + parts_text(r.parts) : "no") +
        ". Tor: " + (r.tor ? "yes" : "no, the VPN") +
        ". Cookies: " + (r.cookies_on ? "allowed" : "blocked") +
        ". JavaScript: " + (r.js ? "allowed" : "blocked") +
        ". Exempt from lies: " + (r.exempt ? "yes" : "no") +
        ". Profile: " + r.face +
        ". Circuit: " + r.circuit +
        ". Exit IP: " + ip_text(r) + ".";
    document.getElementById("b_keep").textContent =
        r.kept ? "Do not keep [p]" : "Keep [p]";
    fill("d_storage", [], function () {});
    send({ cmd: "detail", host: host }).then(function (d) {
        if (current() && current().host == host) {
            show_detail(d);
        }
    });
}

function refocus()
{
    var i;

    i = sel;
    return load().then(function () {
        focus_row(i);
    });
}

function marked_hosts()
{
    var out;
    var h;

    out = [];
    for (h in marked) {
        out.push(h);
    }
    return out;
}

function toggle_mark(i)
{
    var r;

    if (i < 0 || i >= shown.length) {
        return;
    }
    r = shown[i];
    if (marked[r.host]) {
        delete marked[r.host];
    } else {
        marked[r.host] = 1;
    }
    render();
    focus_row(i);
}

function mark_all(on)
{
    var i;

    if (!on) {
        marked = {};
    }
    for (i = 0; on && i < shown.length; i++) {
        marked[shown[i].host] = 1;
    }
    render();
    focus_row(sel);
}

/* The marked sites, or else the selected one */
function targets()
{
    var a;
    var r;

    a = marked_hosts();
    if (a.length > 0) {
        return a;
    }
    r = current();
    return r ? [r.host] : [];
}

function is_kept(host)
{
    var i;

    for (i = 0; i < rows.length; i++) {
        if (rows[i].host == host) {
            return rows[i].kept;
        }
    }
    return 0;
}

var PART_NAMES = [
    ["c", "cookies"], ["s", "storage"], ["h", "history"],
    ["p", "settings"], ["f", "profile"]
];
var PART_KEYS = ["cookies", "storage", "history", "settings", "face"];

function parts_text(letters)
{
    var out;
    var i;

    out = [];
    for (i = 0; i < PART_NAMES.length; i++) {
        if (letters.indexOf(PART_NAMES[i][0]) >= 0) {
            out.push(PART_NAMES[i][1]);
        }
    }
    return out.length > 0 ? out.join(", ") : "nothing";
}

function row_of(host)
{
    var i;

    for (i = 0; i < rows.length; i++) {
        if (rows[i].host == host) {
            return rows[i];
        }
    }
    return null;
}

/*
 * Keys 1 to 5: what kept sites keep. When one of them does not keep
 * it (or is not kept at all), they all keep it; else none of them.
 */
function do_part(n)
{
    var a;
    var all;
    var r;
    var i;
    var chain;

    a = targets();
    all = 1;
    for (i = 0; i < a.length; i++) {
        r = row_of(a[i]);
        if (!r || !r.kept ||
            r.parts.indexOf(PART_NAMES[n][0]) < 0)
        {
            all = 0;
        }
    }
    chain = Promise.resolve();
    for (i = 0; i < a.length; i++) {
        chain = chain.then(send.bind(null, {
            cmd: "keep_part", host: a[i], part: PART_KEYS[n], value: !all
        }));
    }
    status((all ? "Not keeping " : "Keeping ") + PART_NAMES[n][1] +
        " for " + a.length + (a.length == 1 ? " site" : " sites"));
    chain.then(refocus);
}

/* Keep them all, or, when all are kept already, keep none of them */
function do_keep()
{
    var a;
    var all;
    var i;
    var chain;

    a = targets();
    all = 1;
    for (i = 0; i < a.length; i++) {
        if (!is_kept(a[i])) {
            all = 0;
        }
    }
    chain = Promise.resolve();
    for (i = 0; i < a.length; i++) {
        chain = chain.then(send.bind(null,
            { cmd: "keep", host: a[i], value: !all }));
    }
    chain.then(refocus);
}

function do_forget()
{
    var a;
    var what;

    a = targets();
    if (a.length == 0) {
        return;
    }
    what = a.length == 1 ? a[0] : a.length + " sites (" +
        a.join(", ") + ")";
    if (window.confirm("Close the tabs of " + what + " and forget " +
        "everything about them, kept or not: cookies, storage, history, " +
        "downloads, closed tabs and their place in your lists?"))
    {
        send({ cmd: "forget", hosts: a }).then(function () {
            marked = {};
            return refocus();
        });
    }
}

/* each(hosts, fn): fn(host) for each, one after another */
function each(hosts, fn)
{
    var chain;
    var i;

    chain = Promise.resolve();
    for (i = 0; i < hosts.length; i++) {
        chain = chain.then(fn.bind(null, hosts[i]));
    }
    return chain;
}

function row_of(host)
{
    var i;

    for (i = 0; i < rows.length; i++) {
        if (rows[i].host == host) {
            return rows[i];
        }
    }
    return null;
}

function names(a)
{
    return a.length == 1 ? a[0] : a.length + " sites (" + a.join(", ") +
        ")";
}

function do_face()
{
    each(targets(), function (h) {
        return send({ cmd: "face", host: h });
    }).then(refocus);
}

function do_hist()
{
    var a;

    a = targets();
    if (a.length > 0 && window.confirm("Clear the history of " +
        names(a) + "?"))
    {
        each(a, function (h) {
            return send({ cmd: "clear_history", host: h });
        }).then(refocus);
    }
}

/*
 * J: allow JavaScript on them all, or, when all are allowed already,
 * on none of them. T: the same for going without Tor.
 */
function do_js()
{
    var a;
    var all;
    var i;

    a = targets();
    all = 1;
    for (i = 0; i < a.length; i++) {
        if (!row_of(a[i]) || !row_of(a[i]).js_site) {
            all = 0;
        }
    }
    each(a, function (h) {
        return send({ cmd: "site", host: h, allow: !all });
    }).then(refocus);
}

function do_cookie()
{
    var a;
    var all;
    var i;

    a = targets();
    all = 1;
    for (i = 0; i < a.length; i++) {
        if (!row_of(a[i]) || !row_of(a[i]).cookie_site) {
            all = 0;
        }
    }
    each(a, function (h) {
        return send({ cmd: "cookie_site", host: h, value: !all });
    }).then(refocus);
}

function do_tor()
{
    var a;
    var all;
    var i;

    a = targets();
    all = 1;
    for (i = 0; i < a.length; i++) {
        if (!row_of(a[i]) || !row_of(a[i]).no_tor) {
            all = 0;
        }
    }
    each(a, function (h) {
        return send({ cmd: "tor_site", host: h, value: all });
    }).then(refocus);
}

function do_storage()
{
    var r;
    var host;

    r = current();
    if (!r) {
        return;
    }
    host = r.host;
    status("reading storage of " + host + "...");
    send({ cmd: "storage", host: host }).then(function (s) {
        var list;
        var i;

        if (!current() || current().host != host) {
            return;
        }
        if (!s.found) {
            status(host + ": open a tab of this site first");
            return;
        }
        status("");
        list = [];
        for (i = 0; i < s.local.length; i++) {
            list.push(["local", s.local[i][0], s.local[i][1]]);
        }
        for (i = 0; i < s.session.length; i++) {
            list.push(["session", s.session[i][0], s.session[i][1]]);
        }
        for (i = 0; i < s.idb.length; i++) {
            list.push(["IndexedDB", s.idb[i], ""]);
        }
        fill("d_storage", list, function (tr, x) {
            cell(tr, x[0], "key");
            cell(tr, x[1], "");
            cell(tr, x[2], "val");
        });
    });
}

function look_up(r)
{
    status("looking up " + r.host + "...");
    return send({ cmd: "ip", host: r.host }).then(function (res) {
        r.ip = res.ip;
        status(res.note != "" ? r.host + ": " + res.note : "");
        render();
        focus_row(sel);
    });
}

function do_ip()
{
    each(targets(), function (h) {
        var r;

        r = row_of(h);
        return r ? look_up(r) : null;
    }).then(function () {
        status("done");
    });
}

function do_all()
{
    var list;
    var i;
    var chain;

    list = shown.slice(0);
    chain = Promise.resolve();
    for (i = 0; i < list.length; i++) {
        chain = chain.then(look_up.bind(null, list[i]));
    }
    chain.then(function () {
        status("done");
    });
}

function do_wipe()
{
    if (window.confirm("Wipe the data of every site that is not kept?")) {
        send({ cmd: "wipe_unkept" }).then(refocus);
    }
}

function on_key(e)
{
    var f;

    f = document.getElementById("filter");
    if (e.target == f) {
        if (e.key == "Enter" || e.key == "Escape") {
            focus_row(0);
            e.preventDefault();
        }
        return;
    }
    if (e.ctrlKey || e.altKey || e.metaKey) {
        return;
    }
    /* our keys are ours: IceCat must not start a find with them */
    if ("jk/xpfciahswru* JTK12345".indexOf(e.key) >= 0 &&
        e.key.length == 1)
    {
        e.preventDefault();
    }
    if (e.key == "j") {
        focus_row(sel + 1);
    } else if (e.key == "k") {
        focus_row(sel - 1);
    } else if (e.key == "/") {
        f.focus();
        e.preventDefault();
    } else if (e.key == "x" || e.key == " ") {
        toggle_mark(sel);
        focus_row(sel + 1);
        e.preventDefault();
    } else if (e.key == "*") {
        mark_all(1);
    } else if (e.key == "u") {
        mark_all(0);
    } else if (e.key == "J") {
        do_js();
    } else if (e.key == "K") {
        do_cookie();
    } else if (e.key == "T") {
        do_tor();
    } else if (e.key == "p") {
        do_keep();
    } else if (e.key == "f") {
        do_forget();
    } else if (e.key == "c") {
        do_face();
    } else if (e.key == "i") {
        do_ip();
    } else if (e.key == "a") {
        do_all();
    } else if (e.key == "h") {
        do_hist();
    } else if (e.key == "s") {
        do_storage();
    } else if (e.key == "w") {
        do_wipe();
    } else if (e.key == "r") {
        refocus();
    } else if (e.key >= "1" && e.key <= "5" && e.key.length == 1) {
        do_part(e.key.charCodeAt(0) - 49);
    }
}

document.getElementById("filter").addEventListener("input", function () {
    sel = -1;
    render();
});
document.getElementById("b_keep").addEventListener("click", do_keep);
document.getElementById("b_forget").addEventListener("click", do_forget);
document.getElementById("b_face").addEventListener("click", do_face);
document.getElementById("b_ip").addEventListener("click", do_ip);
document.getElementById("b_hist").addEventListener("click", do_hist);
document.getElementById("b_storage").addEventListener("click",
    do_storage);
document.addEventListener("keydown", on_key);
/* The page opens in the filter: type to narrow, Enter or Esc to the
   rows, where the keys act; / comes back */
load().then(function () {
    document.getElementById("filter").focus();
});
