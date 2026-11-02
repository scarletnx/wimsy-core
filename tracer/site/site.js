// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// Tracer's site: each date in the reader's time zone, and the lists filtered as the reader types or chooses. The
// filters are kept in the address's #, so a filtered list can be linked to. The pages work without this file.
"use strict";

for (const t of document.querySelectorAll("time[datetime]")) {
  const d = new Date(t.dateTime);
  if (!isNaN(d)) {
    t.title = t.textContent;
    t.textContent = d.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
  }
}

for (const box of document.querySelectorAll(".filters[data-for]")) {
  const list = document.getElementById(box.dataset.for);
  if (!list) continue;
  const rows = [...list.querySelectorAll(list.tagName === "TABLE" ? "tbody tr" : ":scope > li")];
  const search = box.querySelector("[data-search]");
  const selects = [...box.querySelectorAll("select[data-key]")];
  const count = box.querySelector("[data-count]");
  const empty = list.closest("main").querySelector("[data-empty]");

  // each choice's values as the rows have them, the most common first
  for (const s of selects) {
    const seen = new Map();
    for (const r of rows) {
      const v = r.dataset[s.dataset.key];
      if (v) seen.set(v, (seen.get(v) || 0) + 1);
    }
    if (seen.size < 2) continue;
    for (const [v, n] of [...seen].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]))) {
      const o = document.createElement("option");
      o.value = v;
      o.textContent = `${v} (${n})`;
      s.append(o);
    }
    s.hidden = false;
  }

  const read = () => {
    const p = new URLSearchParams(location.hash.slice(1));
    search.value = p.get("q") || "";
    for (const s of selects) s.value = p.get(s.dataset.key) || "";
  };
  const apply = () => {
    const words = search.value.toLowerCase().split(/\s+/).filter(Boolean);
    let shown = 0;
    for (const r of rows) {
      const text = r.dataset.text || "";
      const ok = words.every((w) => text.includes(w)) && selects.every((s) => !s.value || r.dataset[s.dataset.key] === s.value);
      r.hidden = !ok;
      shown += ok;
    }
    if (count) count.textContent = shown === rows.length ? `${rows.length} shown` : `${shown} of ${rows.length} shown`;
    if (empty) empty.hidden = shown !== 0;
  };
  const keep = () => {
    const p = new URLSearchParams();
    if (search.value) p.set("q", search.value);
    for (const s of selects) if (s.value) p.set(s.dataset.key, s.value);
    const hash = p.toString();
    history.replaceState(null, "", hash ? `#${hash}` : location.pathname + location.search);
    apply();
  };
  search.addEventListener("input", keep);
  for (const s of selects) s.addEventListener("change", keep);
  addEventListener("hashchange", () => { read(); apply(); });
  read();
  apply();
}
