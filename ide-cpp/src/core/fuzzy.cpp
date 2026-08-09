#include "fuzzy.h"

int fuzzyRank(const QString &query, const QString &text) {
    if (query.isEmpty()) return 0;
    const QString q = query.toLower();
    const QString t = text.toLower();
    int first = -1;
    int at = 0;
    for (QChar c : q) {
        int hit = t.indexOf(c, at);
        if (hit < 0) return -1;
        if (first < 0) first = hit;
        at = hit + 1;
    }
    return first;
}
