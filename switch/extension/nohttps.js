var args = new URLSearchParams(window.location.search);
var url = args.get("u") || "";
var host = "";

function allow(cmd)
{
    if (host == "") {
        return;
    }
    browser.runtime.sendMessage({ cmd: cmd, host: host, value: 1 })
        .then(function () {
            window.location.replace(url);
        });
}

function back()
{
    if (window.history.length > 1) {
        window.history.back();
    } else {
        window.close();
    }
}

function on_key(e)
{
    if (e.ctrlKey || e.altKey || e.metaKey) {
        return;
    }
    if (e.key == "h") {
        allow("http_once");
    }
    if (e.key == "a") {
        allow("http");
    }
    if (e.key == "b" || e.key == "Escape") {
        back();
    }
}

try {
    if (url.indexOf("http://") == 0) {
        host = new URL(url).hostname;
    }
} catch (e) {
    host = "";
}
document.getElementById("url").textContent = url;
document.getElementById("why").textContent =
    args.get("why") == "loop" ?
    host + " keeps sending you back to plain HTTP." :
    host + " could not be reached over HTTPS.";
document.getElementById("once").addEventListener("click", function () {
    allow("http_once");
});
document.getElementById("always").addEventListener("click", function () {
    allow("http");
});
document.getElementById("back").addEventListener("click", back);
document.addEventListener("keydown", on_key);
document.getElementById("back").focus();
