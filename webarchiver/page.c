/* archive.html and history.html (XHTML 1.0 Transitional, CSS1/CSS2, no
 * scripts), --versions and --restore */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "webarchiver.h"

/* CSS1 plus a little CSS2 (max-width, border-collapse, overflow) */
static const char *page_css[] = {
    "body{font-family:sans-serif;font-size:15px;line-height:1.45;"
    "max-width:1000px;margin:20px auto;padding:0 16px;color:#222;"
    "background:#fff}\n",
    "h1{margin-bottom:4px}h2{margin-top:32px;"
    "border-bottom:1px solid #ccc}\n",
    "table{border-collapse:collapse;width:100%}td,th{padding:5px 8px;"
    "text-align:left;border-bottom:1px solid #e4e4e4;vertical-align:top}\n",
    "th{background:#f4f4f4}.n{color:#777}.ok{color:#18794e}"
    ".warn{color:#a15c00}.small{font-size:13px}\n",
    "code,pre{background:#f3f3f3;font-size:13px}pre{padding:8px;"
    "overflow:auto}\n",
    ".box{border:1px solid #ccc;padding:10px 14px;margin:16px 0;"
    "background:#fafafa}",
    0 };

void page_head(struct buf *b, const char *title, int refresh_secs)
{
    int i;

    buf_add(b, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 "
        "Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/"
        "xhtml1-transitional.dtd\">\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head>"
        "<meta http-equiv=\"Content-Type\" content=\"text/html; "
        "charset=utf-8\" />\n"
        "<meta name=\"viewport\" content=\"width=device-width, "
        "initial-scale=1\" />\n");
    if (refresh_secs) {
        buf_add(b, "<meta http-equiv=\"refresh\" content=\"");
        buf_addll(b, refresh_secs);
        buf_add(b, "\" />\n");
    }
    buf_add(b, "<title>");
    buf_addhtml(b, title);
    buf_add(b, "</title><style type=\"text/css\">");
    for (i = 0; page_css[i]; i++)
        buf_add(b, page_css[i]);
    buf_add(b, "</style></head><body>\n");
}

int run_active(void)
{
    char *p;
    int res;

    p = path2(out_dir, ".archive-lock");
    res = is_dir(p);
    free(p);
    return res;
}

/* link to a site folder: its index.html if there is one */
static void add_start_page(struct buf *b, const char *folder)
{
    static const char *names[] = { "index.html", "index.htm", 0 };
    char *dir, *p, *q;
    int i;

    q = pct_quote(folder, "/~@!$&'()*+,;=-._");
    buf_add(b, q);
    free(q);
    buf_addc(b, '/');
    dir = path2(out_dir, folder);
    for (i = 0; names[i]; i++) {
        p = path2(dir, names[i]);
        if (is_file(p)) {
            buf_add(b, names[i]);
            free(p);
            break;
        }
        free(p);
    }
    free(dir);
}

/* ------------------------------------------------------------ archive.html */

struct site {
    int complete;
    int busy;
    int slow;
    int files;
    long long size;
    int versions;
    char updated[32];
    char *base;
};

static void site_info(const char *d, struct site *s)
{
    struct dstate st;
    struct strlist ids, keys;
    struct table files;
    char *p, *data;
    const char *v;
    int i, len;

    state_load(d, &st);
    s->complete = st.complete;
    s->busy = !strcmp(st.phase, "crawl") || !strcmp(st.phase, "finalize");
    p = state_file(d, ".slow");
    s->slow = is_file(p);
    free(p);
    sl_init(&ids);
    versions_of(d, &ids);
    s->versions = ids.n;
    strcpy(s->updated, "-");
    tab_init(&files);
    if (ids.n) {
        version_files(d, ids.v[ids.n - 1], &files);
        nice_time(ids.v[ids.n - 1], s->updated);
    } else {
        cache_load(d, &files);
    }
    s->files = files.count;
    s->size = 0;
    sl_init(&keys);
    tab_keys(&files, &keys);
    for (i = 0; i < keys.n; i++) {
        v = tab_get(&files, keys.v[i]);
        if (strlen(v) > 65)
            s->size += parse_ll(v + 65);
    }
    sl_free(&keys);
    tab_free(&files);
    sl_free(&ids);
    if (!s->files) {
        p = domain_dir(d);
        s->size = dir_size(p);
        free(p);
    }
    p = state_file(d, ".base");
    data = read_file(p, &len);
    free(p);
    s->base = data ? data : xstrdup("");
}

void render_archive(struct buf *b, int served, const char *note)
{
    struct strlist ds;
    struct site *info;
    char tmp[64], when[32];
    long long total, hist;
    int i, busy;

    sl_init(&ds);
    all_domains(&ds);
    info = xmalloc((ds.n + 1) * sizeof(struct site));
    total = 0;
    for (i = 0; i < ds.n; i++) {
        site_info(ds.v[i], &info[i]);
        total += info[i].size;
    }
    hist = is_dir(obj_dir) ? dir_size(obj_dir) : 0;
    busy = run_active();
    page_head(b, "Website archive", served && busy ? 10 : 0);
    buf_add(b, "<h1>Website archive</h1><p class=\"n\">");
    buf_addll(b, ds.n);
    buf_add(b, ds.n == 1 ? " site" : " sites");
    human(total, tmp);
    buf_add(b, " &middot; ");
    buf_add(b, tmp);
    buf_add(b, " in current versions &middot; ");
    human(hist, tmp);
    buf_add(b, tmp);
    buf_add(b, " of history &middot; page made ");
    utc_id(now(), tmp);
    nice_time(tmp, when);
    buf_add(b, when);
    buf_add(b, " UTC &middot; <a href=\"history.html\" accesskey=\"h\">"
        "all versions</a></p>");
    if (note && note[0]) {
        buf_add(b, "<div class=\"box\">");
        buf_add(b, note);
        buf_add(b, "</div>");
    }
    if (busy) {
        buf_add(b, "<div class=\"box\"><b class=\"warn\">A download is "
            "running right now.</b> ");
        buf_add(b, served ? "This page refreshes every 10 seconds. "
            "<a href=\"run-log\" accesskey=\"p\">Show progress</a>" :
            "Reload this page when it has finished.");
        buf_add(b, "</div>");
    }
    buf_add(b, "<table><tr><th>Site</th><th>Status</th><th>Files</th>"
        "<th>Size</th><th>Versions</th><th>Last saved</th><th>Links</th>"
        "</tr>");
    for (i = 0; i < ds.n; i++) {
        buf_add(b, "<tr><td><b>");
        buf_addhtml(b, ds.v[i]);
        buf_add(b, "</b></td><td>");
        if (info[i].busy && busy)
            buf_add(b, "<span class=\"warn\">in progress</span>");
        else if (info[i].complete)
            buf_add(b, "<span class=\"ok\">complete</span>");
        else
            buf_add(b, "<span class=\"warn\">unfinished</span><br />"
                "<span class=\"n small\">run again to continue</span>");
        if (info[i].slow)
            buf_add(b, "<br /><span class=\"n small\">site asked for a "
                "slower pace</span>");
        buf_add(b, "</td><td>");
        buf_addll(b, info[i].files);
        buf_add(b, "</td><td>");
        human(info[i].size, tmp);
        buf_add(b, tmp);
        buf_add(b, "</td><td>");
        buf_addll(b, info[i].versions);
        buf_add(b, "</td><td>");
        buf_add(b, info[i].updated);
        buf_add(b, "</td><td><a href=\"");
        add_start_page(b, ds.v[i]);
        buf_add(b, "\">open</a>");
        if (info[i].versions) {
            char *q;

            q = pct_quote(ds.v[i], "~-._");
            buf_add(b, " &middot; <a href=\"history.html#");
            buf_add(b, q);
            buf_add(b, "\">history</a>");
            free(q);
        }
        if (info[i].base[0]) {
            buf_add(b, " &middot; <a href=\"");
            buf_addhtml(b, info[i].base);
            buf_add(b, "\">live site</a>");
        }
        buf_add(b, "</td></tr>");
        free(info[i].base);
    }
    if (!ds.n)
        buf_add(b, "<tr><td colspan=\"7\" class=\"n\">No sites yet."
            "</td></tr>");
    buf_add(b, "</table>");
    if (served) {
        buf_add(b, "<h2>Add websites</h2><form method=\"post\" "
            "action=\"add\"><p>One link per line (for example "
            "<code>example.com</code> or "
            "<code>https://example.org/blog/</code>):</p>"
            "<p><textarea name=\"links\" rows=\"5\" cols=\"70\" "
            "accesskey=\"a\">"
            "</textarea></p><p><input type=\"submit\" value=\"Download "
            "into the archive\" /></p></form>");
        buf_add(b, "<form method=\"post\" action=\"update-all\"><p><input "
            "type=\"submit\" value=\"Update the whole archive\" "
            "accesskey=\"u\" /> "
            "<span class=\"n small\">checks every site for changes and "
            "saves new versions</span></p></form>");
        buf_add(b, "<p class=\"n small\">Keyboard: access key a goes to the "
            "links box, u updates the archive, h opens all versions, p "
            "the progress (Alt+key, or Alt+Shift+key in Firefox). Tab "
            "moves between links and buttons, Enter follows them.</p>");
        if (busy) {
            buf_add(b, "<p class=\"n small\">New requests wait until the "
                "current download has finished.</p>");
        }
    } else {
        buf_add(b, "<p class=\"n small\">To add websites from this page, "
            "start it with <code>webarchiver --serve .</code> inside this "
            "directory and open <code>http://127.0.0.1:8800/</code>.</p>");
    }
    buf_add(b, "</body></html>\n");
    free(info);
    sl_free(&ds);
}

/* ------------------------------------------------------------ history.html */

void render_history(struct buf *b, int served)
{
    struct strlist ds, ids, changes;
    struct table files;
    struct strlist keys;
    char tmp[64], when[32];
    char *q, *dir, *restored;
    long long size;
    int i, j, k, n, partial, counts[3];

    sl_init(&ds);
    list_dir(hist_dir, &ds);
    page_head(b, "Archive history", 0);
    utc_id(now(), tmp);
    nice_time(tmp, when);
    buf_add(b, "<h1>Archive history</h1><p class=\"n\"><a href=\""
        "archive.html\">&larr; archive</a> &middot; updated ");
    buf_add(b, when);
    buf_add(b, " UTC. Each site folder is always its current version; "
        "older versions are rebuilt on request.</p>");
    for (i = 0; i < ds.n; i++) {
        if (!strcmp(ds.v[i], "objects"))
            continue;
        sl_init(&ids);
        versions_of(ds.v[i], &ids);
        q = pct_quote(ds.v[i], "~-._");
        buf_add(b, "\n<h2><a name=\"");
        buf_add(b, q);
        buf_add(b, "\" id=\"");
        buf_add(b, q);
        buf_add(b, "\">");
        buf_addhtml(b, ds.v[i]);
        buf_add(b, "</a></h2><p><a href=\"");
        add_start_page(b, ds.v[i]);
        buf_add(b, "\">Open current version</a></p><table><tr><th>#</th>"
            "<th>Saved</th><th>Files</th><th>Changes</th><th>Open</th>"
            "</tr>");
        free(q);
        for (j = ids.n - 1; j >= 0; j--) {
            n = j + 1;
            tab_init(&files);
            version_files(ds.v[i], ids.v[j], &files);
            sl_init(&keys);
            tab_keys(&files, &keys);
            size = 0;
            for (k = 0; k < keys.n; k++)
                size += parse_ll(tab_get(&files, keys.v[k]) + 65);
            sl_init(&changes);
            version_changes(ds.v[i], ids.v[j], &changes);
            counts[0] = 0;
            counts[1] = 0;
            counts[2] = 0;
            for (k = 0; k < changes.n; k++) {
                if (changes.v[k][0] == '+')
                    counts[0]++;
                else if (changes.v[k][0] == '~')
                    counts[1]++;
                else
                    counts[2]++;
            }
            partial = version_info(ds.v[i], ids.v[j], "partial");
            buf_add(b, "\n<tr><td>");
            buf_addll(b, n);
            buf_add(b, "</td><td>");
            nice_time(ids.v[j], when);
            buf_add(b, when);
            buf_add(b, "</td><td>");
            buf_addll(b, keys.n);
            buf_add(b, "<br /><span class=\"n\">");
            human(size, tmp);
            buf_add(b, tmp);
            buf_add(b, "</span></td><td>+");
            buf_addll(b, counts[0]);
            buf_add(b, " new, ~");
            buf_addll(b, counts[1]);
            buf_add(b, " changed, -");
            buf_addll(b, counts[2]);
            buf_add(b, " removed");
            if (partial)
                buf_add(b, " <span class=\"n\">(interrupted run)</span>");
            if (n > 1 && changes.n) {
                buf_add(b, "<br /><span class=\"small n\">");
                for (k = 0; k < changes.n && k < 25; k++) {
                    if (k)
                        buf_add(b, "<br />");
                    buf_addc(b, changes.v[k][0]);
                    buf_addc(b, ' ');
                    buf_addhtml(b, changes.v[k] + 2);
                }
                if (changes.n > 25) {
                    buf_add(b, "<br />... ");
                    buf_addll(b, changes.n - 25);
                    buf_add(b, " more (see <code>--versions ");
                    buf_addhtml(b, ds.v[i]);
                    buf_add(b, "</code>)");
                }
                buf_add(b, "</span>");
            }
            buf_add(b, "</td><td>");
            restored = str3(ds.v[i], "@", ids.v[j]);
            dir = path2(out_dir, restored);
            if (j == ids.n - 1) {
                buf_add(b, "<a href=\"");
                add_start_page(b, ds.v[i]);
                buf_add(b, "\">current</a>");
            } else if (is_dir(dir)) {
                buf_add(b, "<a href=\"");
                add_start_page(b, restored);
                buf_add(b, "\">open</a>");
            } else if (served) {
                buf_add(b, "<form method=\"post\" action=\"restore\">"
                    "<input type=\"hidden\" name=\"domain\" value=\"");
                buf_addhtml(b, ds.v[i]);
                buf_add(b, "\" /><input type=\"hidden\" name=\"version\" "
                    "value=\"");
                buf_addll(b, n);
                buf_add(b, "\" /><input type=\"submit\" value=\"Rebuild\" "
                    "/></form>");
            } else {
                buf_add(b, "<code class=\"small\">webarchiver --restore ");
                buf_addhtml(b, ds.v[i]);
                buf_addc(b, ' ');
                buf_addll(b, n);
                buf_add(b, " .</code>");
            }
            buf_add(b, "</td></tr>");
            free(dir);
            free(restored);
            sl_free(&changes);
            sl_free(&keys);
            tab_free(&files);
        }
        buf_add(b, "</table>");
        sl_free(&ids);
    }
    if (!ds.n || (ds.n == 1 && !strcmp(ds.v[0], "objects")))
        buf_add(b, "<p>No finished downloads yet.</p>");
    buf_add(b, "\n</body></html>\n");
    sl_free(&ds);
}

static void write_page(const char *name, struct buf *b)
{
    char *p;

    p = path2(out_dir, name);
    write_file(p, b->s, b->len);
    free(p);
}

/* the static pages, which work when opened straight from disk */
void write_pages(void)
{
    struct buf b;

    buf_init(&b);
    render_history(&b, 0);
    write_page("history.html", &b);
    buf_free(&b);
    buf_init(&b);
    render_archive(&b, 0, 0);
    write_page("archive.html", &b);
    buf_free(&b);
}

/* ------------------------------------------------------------ --versions */

void cmd_versions(const char *only)
{
    struct strlist ds, ids, keys, changes;
    struct table files;
    char size[32], line[256];
    long long total;
    int i, j, k, shown, counts[3];
    static const char kinds[] = "+~-";

    sl_init(&ds);
    if (only) {
        sl_add(&ds, only);
    } else {
        struct strlist names;

        sl_init(&names);
        list_dir(hist_dir, &names);
        for (i = 0; i < names.n; i++) {
            if (strcmp(names.v[i], "objects"))
                sl_add(&ds, names.v[i]);
        }
        sl_free(&names);
    }
    if (!ds.n)
        say("No versions saved yet.", 0, 0);
    for (i = 0; i < ds.n; i++) {
        sl_init(&ids);
        versions_of(ds.v[i], &ids);
        sprintf(line, "  (%d version%s)", ids.n, ids.n == 1 ? "" : "s");
        say(ds.v[i], line, 0);
        for (j = 0; j < ids.n; j++) {
            tab_init(&files);
            version_files(ds.v[i], ids.v[j], &files);
            sl_init(&keys);
            tab_keys(&files, &keys);
            total = 0;
            for (k = 0; k < keys.n; k++)
                total += parse_ll(tab_get(&files, keys.v[k]) + 65);
            human(total, size);
            sl_init(&changes);
            version_changes(ds.v[i], ids.v[j], &changes);
            counts[0] = 0;
            counts[1] = 0;
            counts[2] = 0;
            for (k = 0; k < changes.n; k++)
                counts[strchr(kinds, changes.v[k][0]) - kinds]++;
            sprintf(line, "  %3d  %s  %6d files  %10s   +%d new  "
                "~%d changed  -%d removed%s%s", j + 1, ids.v[j], keys.n,
                size, counts[0], counts[1], counts[2],
                version_info(ds.v[i], ids.v[j], "partial") ?
                "  (interrupted run)" : "",
                j == ids.n - 1 ? "  <- current" : "");
            say(line, 0, 0);
            if (only && j > 0) {
                shown = 0;
                for (k = 0; k < changes.n; k++) {
                    if (shown < 150) {
                        printf("         %c %s\n", changes.v[k][0],
                            changes.v[k] + 2);
                        shown++;
                    }
                }
                if (changes.n > shown)
                    printf("         ... %d more\n", changes.n - shown);
            }
            sl_free(&changes);
            sl_free(&keys);
            tab_free(&files);
        }
        sl_free(&ids);
    }
    fflush(stdout);
    sl_free(&ds);
}

/* ------------------------------------------------------------ --restore */

/* rebuild an old version as DIR/<domain>@<version>/ */
int cmd_restore(const char *d, const char *which)
{
    struct strlist ids, keys, live;
    struct table files, cache, where;
    char *id, *dest, *tmpdest, *src, *out, *outdir;
    const char *h;
    char sha[65], num[32];
    int i, n, missing;

    sl_init(&ids);
    versions_of(d, &ids);
    if (!ids.n) {
        say("No versions saved for ", d, "");
        return 1;
    }
    id = 0;
    n = (int)parse_ll(which);
    if (!strcmp(which, "latest")) {
        id = ids.v[ids.n - 1];
    } else if (which[0] >= '0' && which[0] <= '9' && n >= 1 &&
        n <= ids.n && strlen(which) < 6)
    {
        id = ids.v[n - 1];
    } else {
        for (i = 0; i < ids.n; i++) {
            if (starts_with(ids.v[i], which)) {
                if (id) {
                    id = 0;
                    break;
                }
                id = ids.v[i];
            }
        }
    }
    if (!id) {
        say("Unknown version ", which, " - see --versions");
        return 1;
    }
    src = str3(d, "@", id);
    dest = path2(out_dir, src);
    free(src);
    if (is_dir(dest)) {
        say("Already restored: ", dest, "");
        return 1;
    }
    /* where each content is now in the live folder */
    cache_load(d, &cache);
    tab_init(&where);
    sl_init(&live);
    src = domain_dir(d);
    list_files(src, &live);
    free(src);
    for (i = 0; i < live.n; i++) {
        if (ends_with(live.v[i], ".part") || ends_with(live.v[i], ".tmp"))
            continue;
        h = hash_of(&cache, live.v[i]);
        if (!tab_get(&where, h))
            tab_set(&where, h, live.v[i]);
    }
    sl_free(&live);
    cache_save(d, &cache);
    tab_init(&files);
    version_files(d, id, &files);
    sl_init(&keys);
    tab_keys(&files, &keys);
    tmpdest = str2(dest, ".partial");
    missing = 0;
    for (i = 0; i < keys.n; i++) {
        memcpy(sha, tab_get(&files, keys.v[i]), 64);
        sha[64] = 0;
        src = obj_path(sha);
        if (!is_file(src)) {
            free(src);
            src = tab_get(&where, sha) ? xstrdup(tab_get(&where, sha)) : 0;
        }
        out = path2(tmpdest, keys.v[i]);
        outdir = xstrndup(out, strrchr(out, '/') - out);
        mkdirs(outdir);
        if (!src || !copy_file(src, out))
            missing++;
        free(outdir);
        free(out);
        free(src);
    }
    rename(tmpdest, dest);
    sprintf(num, " (%d files) to:", keys.n - missing);
    printf("Restored %s version %s%s\n  %s\n", d, id, num, dest);
    if (missing)
        printf("  %d file(s) could not be recovered\n", missing);
    fflush(stdout);
    write_pages();
    free(tmpdest);
    free(dest);
    sl_free(&keys);
    tab_free(&files);
    tab_free(&where);
    tab_free(&cache);
    sl_free(&ids);
    return 0;
}
