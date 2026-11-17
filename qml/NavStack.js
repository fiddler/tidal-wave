.pragma library

// The back history as a pure function, so it can be tested without the window.
//
// `history` is the pages behind the current one, oldest first, as
// {page, params}. Returns the history to keep after moving from `current` to
// `target`. goBack() does not come through here: a pop must not push.

function sameParams(a, b) {
    return JSON.stringify(a || {}) === JSON.stringify(b || {})
}

function forward(history, current, target, detailPages, max) {
    // Home, search and collection are one click away on the sidebar: history
    // starts there rather than leading back out of them.
    if (detailPages.indexOf(target.page) === -1) return []

    var top = history.length > 0 ? history[history.length - 1] : null
    // Forward onto the entry on top is going back. Recording it would make the
    // next back a step forward, which is the two-page bounce one level deep.
    // A match deeper in the stack still pushes: that page was walked away from
    // since, and truncating there would hand back a path nobody took.
    if (top && top.page === target.page && sameParams(top.params, target.params))
        return history.slice(0, -1)

    // Album A to album B is a real step, same page type or not. The same page
    // with the same params is a reload, not a step.
    if (current.page === target.page && sameParams(current.params, target.params))
        return history

    var h = history.slice()
    h.push({ page: current.page, params: current.params })
    return h.length > max ? h.slice(h.length - max) : h
}
