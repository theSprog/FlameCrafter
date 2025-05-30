static constexpr const char* FLAMEGRAPH_JS = R"mytag114514(
function init(evt) {
    details = document.getElementById("details").firstChild;
    searchbtn = document.getElementById("search");
    ignorecaseBtn = document.getElementById("ignorecase");
    unzoombtn = document.getElementById("unzoom");
    matchedtxt = document.getElementById("matched");
    svg = document.getElementsByTagName("svg")[0];
    searching = 0;
    currentSearchTerm = null;
    ignorecase = false;

    var params = get_params();
    if (params.x && params.y)
        zoom(find_group(document.querySelector('[x="' + params.x + '"][y="' + params.y + '"]')));
    if (params.s) search(params.s);
}

window.addEventListener("load", function() {
    var el = document.getElementById("frames").children;
    for (var i = 0; i < el.length; i++) {
        update_text(el[i]);
    }
});


// 事件监听器
window.addEventListener("click", function(e) {
    var target = find_group(e.target);
    if (target) {
        if (target.nodeName == "a") {
            if (e.ctrlKey === false) return;
            e.preventDefault();
        }
        if (target.classList.contains("parent")) unzoom(true);
        zoom(target);
        if (!document.querySelector('.parent')) {
            var params = get_params();
            if (params.x) delete params.x;
            if (params.y) delete params.y;
            history.replaceState(null, null, parse_params(params));
            unzoombtn.classList.add("hide");
            return;
        }

        var el = target.querySelector("rect");
        if (el && el.attributes && el.attributes.y && el.attributes._orig_x) {
            var params = get_params()
            params.x = el.attributes._orig_x.value;
            params.y = el.attributes.y.value;
            history.replaceState(null, null, parse_params(params));
        }
    }
    else if (e.target.id == "unzoom") clearzoom();
    else if (e.target.id == "search") search_prompt();
    else if (e.target.id == "ignorecase") toggle_ignorecase();
}, false)

// 鼠标悬停
window.addEventListener("mouseover", function(e) {
    var target = find_group(e.target);
    if (target) details.nodeValue = nametype + " " + g_to_text(target);
}, false)

window.addEventListener("mouseout", function(e) {
    var target = find_group(e.target);
    if (target) details.nodeValue = ' ';
}, false)

// 键盘快捷键
window.addEventListener("keydown",function (e) {
    if (e.keyCode === 114 || (e.ctrlKey && e.keyCode === 70)) {
        e.preventDefault();
        search_prompt();
    }
    else if (e.ctrlKey && e.keyCode === 73) {
        e.preventDefault();
        toggle_ignorecase();
    }
}, false)

// 辅助函数
function get_params() {
    var params = {};
    var paramsarr = window.location.search.substr(1).split('&');
    for (var i = 0; i < paramsarr.length; ++i) {
        var tmp = paramsarr[i].split("=");
        if (!tmp[0] || !tmp[1]) continue;
        params[tmp[0]]  = decodeURIComponent(tmp[1]);
    }
    return params;
}

function parse_params(params) {
    var uri = "?";
    for (var key in params) {
        uri += key + '=' + encodeURIComponent(params[key]) + '&';
    }
    if (uri.slice(-1) == "&")
        uri = uri.substring(0, uri.length - 1);
    if (uri == '?')
        uri = window.location.href.split('?')[0];
    return uri;
}

function find_child(node, selector) {
    var children = node.querySelectorAll(selector);
    if (children.length) return children[0];
}

function find_group(node) {
    var parent = node.parentElement;
    if (!parent) return;
    if (parent.id == "frames") return node;
    return find_group(parent);
}

function orig_save(e, attr, val) {
    if (e.attributes["_orig_" + attr] != undefined) return;
    if (e.attributes[attr] == undefined) return;
    if (val == undefined) val = e.attributes[attr].value;
    e.setAttribute("_orig_" + attr, val);
}

function orig_load(e, attr) {
    if (e.attributes["_orig_"+attr] == undefined) return;
    e.attributes[attr].value = e.attributes["_orig_" + attr].value;
    e.removeAttribute("_orig_"+attr);
}

function g_to_text(e) {
    var text = find_child(e, "title").firstChild.nodeValue;
    return (text)
}

function g_to_func(e) {
    var func = g_to_text(e);
    if (func != null)
        func = func.replace(/ \([^(]*\)$/, "");
    return (func);
}

function update_text(e) {
    var r = find_child(e, "rect");
    var t = find_child(e, "text");
    var title = find_child(e, "title");
    if (!r || !t || !title) {
        return;
    }
    var w = parseFloat(r.attributes.width.value) -3;
    var txt = find_child(e, "title").textContent.replace(/\([^(]*\)$/, "");
    t.attributes.x.value = parseFloat(r.attributes.x.value) + 3;

    if (w < 2 * fontsize * fontwidth) {
        t.textContent = "";
        return;
    }

    t.textContent = txt;
    var sl = t.getSubStringLength(0, txt.length);
    if (/^ *$/.test(txt) || sl < w)
        return;

    var start = Math.floor((w/sl) * txt.length);
    for (var x = start; x > 0; x = x-2) {
        if (t.getSubStringLength(0, x + 2) <= w) {
            t.textContent = txt.substring(0, x) + "..";
            return;
        }
    }
    t.textContent = "";
}

// zoom
function zoom_reset(e) {
    if (e.attributes != undefined) {
        orig_load(e, "x");
        orig_load(e, "width");
    }
    if (e.childNodes == undefined) return;
    for (var i = 0, c = e.childNodes; i < c.length; i++) {
        zoom_reset(c[i]);
    }
}

function zoom_child(e, x, ratio) {
    if (e.attributes != undefined) {
        if (e.attributes.x != undefined) {
            orig_save(e, "x");
            e.attributes.x.value = (parseFloat(e.attributes.x.value) - x - xpad) * ratio + xpad;
            if (e.tagName == "text")
                e.attributes.x.value = find_child(e.parentNode, "rect[x]").attributes.x.value + 3;
        }
        if (e.attributes.width != undefined) {
            orig_save(e, "width");
            e.attributes.width.value = parseFloat(e.attributes.width.value) * ratio;
        }
    }

    if (e.childNodes == undefined) return;
    for (var i = 0, c = e.childNodes; i < c.length; i++) {
        zoom_child(c[i], x - xpad, ratio);
    }
}

function zoom_parent(e) {
    if (e.attributes) {
        if (e.attributes.x != undefined) {
            orig_save(e, "x");
            e.attributes.x.value = xpad;
        }
        if (e.attributes.width != undefined) {
            orig_save(e, "width");
            e.attributes.width.value = parseInt(svg.width.baseVal.value) - (xpad * 2);
        }
    }
    if (e.childNodes == undefined) return;
    for (var i = 0, c = e.childNodes; i < c.length; i++) {
        zoom_parent(c[i]);
    }
}

function zoom(node) {
    var rect = find_child(node, "rect");
    if (!rect || !rect.attributes) return;
    var attr = rect.attributes;
    var width = parseFloat(attr.width.value);
    var xmin = parseFloat(attr.x.value);
    var xmax = parseFloat(xmin + width);
    var ymin = parseFloat(attr.y.value);
    var ratio = (svg.width.baseVal.value - 2 * xpad) / width;

    var fudge = 0.0001;

    unzoombtn.classList.remove("hide");

    var el = document.getElementById("frames").children;
    for (var i = 0; i < el.length; i++) {
        var e = el[i];
        var a = find_child(e, "rect").attributes;
        var ex = parseFloat(a.x.value);
        var ew = parseFloat(a.width.value);
        var upstack;
        if (inverted == false) {
            upstack = parseFloat(a.y.value) > ymin;
        } else {
            upstack = parseFloat(a.y.value) < ymin;
        }
        if (upstack) {
            if (ex <= xmin && (ex+ew+fudge) >= xmax) {
                e.classList.add("parent");
                zoom_parent(e);
                update_text(e);
            }
            else
                e.classList.add("hide");
        }
        else {
            if (ex < xmin || ex + fudge >= xmax) {
                e.classList.add("hide");
            }
            else {
                zoom_child(e, xmin, ratio);
                update_text(e);
            }
        }
    }
    search();
}

function unzoom(dont_update_text) {
    unzoombtn.classList.add("hide");
    var el = document.getElementById("frames").children;
    for(var i = 0; i < el.length; i++) {
        el[i].classList.remove("parent");
        el[i].classList.remove("hide");
        zoom_reset(el[i]);
        if(!dont_update_text) update_text(el[i]);
    }
    search();
}

function clearzoom() {
    unzoom();

    var params = get_params();
    if (params.x) delete params.x;
    if (params.y) delete params.y;
    history.replaceState(null, null, parse_params(params));
}

// search
function toggle_ignorecase() {
    ignorecase = !ignorecase;
    if (ignorecase) {
        ignorecaseBtn.classList.add("show");
    } else {
        ignorecaseBtn.classList.remove("show");
    }
    reset_search();
    search();
}

function reset_search() {
    var el = document.querySelectorAll("#frames rect");
    for (var i = 0; i < el.length; i++) {
        orig_load(el[i], "fill")
    }
    var params = get_params();
    delete params.s;
    history.replaceState(null, null, parse_params(params));
}

function search_prompt() {
    if (!searching) {
        var term = prompt("Enter a search term (regexp " +
            "allowed, eg: ^ext4_)"
            + (ignorecase ? ", ignoring case" : "")
            + "\nPress Ctrl-i to toggle case sensitivity", "");
        if (term != null) search(term);
    } else {
        reset_search();
        searching = 0;
        currentSearchTerm = null;
        searchbtn.classList.remove("show");
        searchbtn.firstChild.nodeValue = "Search"
        matchedtxt.classList.add("hide");
        matchedtxt.firstChild.nodeValue = ""
    }
}

function search(term) {
    if (term) currentSearchTerm = term;
    if (currentSearchTerm === null) return;

    var re = new RegExp(currentSearchTerm, ignorecase ? 'i' : '');
    var el = document.getElementById("frames").children;
    var matches = new Object();
    var maxwidth = 0;
    for (var i = 0; i < el.length; i++) {
        var e = el[i];
        var func = g_to_func(e);
        var rect = find_child(e, "rect");
        if (func == null || rect == null)
            continue;

        var w = parseFloat(rect.attributes.width.value);
        if (w > maxwidth)
            maxwidth = w;

        if (func.match(re)) {
            var x = parseFloat(rect.attributes.x.value);
            orig_save(rect, "fill");
            rect.attributes.fill.value = searchcolor;

            if (matches[x] == undefined) {
                matches[x] = w;
            } else {
                if (w > matches[x]) {
                    matches[x] = w;
                }
            }
            searching = 1;
        }
    }
    if (!searching)
        return;
    var params = get_params();
    params.s = currentSearchTerm;
    history.replaceState(null, null, parse_params(params));

    searchbtn.classList.add("show");
    searchbtn.firstChild.nodeValue = "Reset Search";

    var count = 0;
    var lastx = -1;
    var lastw = 0;
    var keys = Array();
    for (k in matches) {
        if (matches.hasOwnProperty(k))
            keys.push(k);
    }
    keys.sort(function(a, b){
        return a - b;
    });
    var fudge = 0.0001;
    for (var k in keys) {
        var x = parseFloat(keys[k]);
        var w = matches[keys[k]];
        if (x >= lastx + lastw - fudge) {
            count += w;
            lastx = x;
            lastw = w;
        }
    }
    matchedtxt.classList.remove("hide");
    var pct = 100 * count / maxwidth;
    if (pct != 100) pct = pct.toFixed(1)
    matchedtxt.firstChild.nodeValue = "Matched: " + pct + "%";
}

function searchover(e) {
    searchbtn.style.opacity = "1.0";
}

function searchout(e) {
    searchbtn.style.opacity = searching ? "1.0" : "0.1";
}
)mytag114514"; // 结束 FLAMEGRAPH_JS