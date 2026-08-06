/**
 * Dev Day terminal content endpoint.
 *
 * Turns a calendar feed into the schema-1 document the terminal polls, so the
 * device keeps itself current with no laptop attached. Deploy it, then point a
 * terminal at it once:
 *
 *   npx wrangler secret put ICS_URL     # your calendar's private iCal address
 *   npx wrangler deploy
 *   tools/dash_sync.py --wifi "SSID" --wifi-password "..." \
 *                      --content-url https://<your-worker>.workers.dev --reboot
 *
 * Two firmware constraints drive the shape of this file, and both fail silently
 * if you get them wrong — the terminal simply keeps showing its previous screen:
 *
 *   1. The response MUST carry Content-Length. The device reads the raw socket,
 *      so chunked framing would arrive inline and be parsed as JSON. Returning a
 *      *string* body gets Content-Length set for you; returning a stream does
 *      not. Verify with:  curl -sI <url> | grep -i 'content-length\|transfer-encoding'
 *   2. The body must stay under 12 KB. Four events is all the panel draws.
 */

const MAX_BYTES = 12000; // CONTENT_MAX_BYTES in firmware/config.h
const MAX_EVENTS = 4; // CardContent::AGENDA_MAX in firmware/content.h
// Sunday-based, matching the weekday index partsIn() produces.
const WEEKDAYS = { SU: 0, MO: 1, TU: 2, WE: 3, TH: 4, FR: 5, SA: 6 };

/** Rejoin RFC 5545 folded lines (continuations begin with space or tab). */
function unfold(text) {
  const out = [];
  for (const raw of text.replace(/\r\n/g, "\n").replace(/\r/g, "\n").split("\n")) {
    if ((raw.startsWith(" ") || raw.startsWith("\t")) && out.length) {
      out[out.length - 1] += raw.slice(1);
    } else {
      out.push(raw);
    }
  }
  return out;
}

const unescape_ = (v) =>
  v.replace(/\\n/gi, " ").replace(/\\,/g, ",").replace(/\\;/g, ";").replace(/\\\\/g, "\\").trim();

/**
 * Parse DTSTART into civil {y, m, d, hh, mm} in the display timezone.
 *
 * Only a trailing Z is a real instant needing conversion. Floating and TZID
 * values are already civil times — round-tripping those through Date would
 * shift them by the zone offset and put a 09:30 standup at 03:30.
 */
function parseDate(value, params, tz) {
  const v = value.trim();
  if (params.VALUE === "DATE" || /^\d{8}$/.test(v)) {
    return {
      parts: { y: +v.slice(0, 4), m: +v.slice(4, 6), d: +v.slice(6, 8), hh: "00", mm: "00" },
      allDay: true,
    };
  }
  const m = v.match(/^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})(Z?)$/);
  if (!m) return null;
  const [, y, mo, d, h, mi, s, z] = m;
  if (z === "Z") {
    return {
      parts: partsIn(new Date(Date.UTC(+y, +mo - 1, +d, +h, +mi, +s)), tz),
      allDay: false,
    };
  }
  return { parts: { y: +y, m: +mo, d: +d, hh: h, mm: mi }, allDay: false };
}

/** Day of week (Sun=0) for a civil date. */
function dowOf(p) {
  return new Date(Date.UTC(p.y, p.m - 1, p.d)).getUTCDay();
}

/** Civil date parts for a Date in a named timezone. */
function partsIn(date, tz) {
  const fmt = new Intl.DateTimeFormat("en-US", {
    timeZone: tz, year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", hour12: false, weekday: "short",
  });
  const p = Object.fromEntries(fmt.formatToParts(date).map((x) => [x.type, x.value]));
  return {
    y: +p.year, m: +p.month, d: +p.day,
    hh: p.hour === "24" ? "00" : p.hour, mm: p.minute,
    dow: ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"].indexOf(p.weekday),
  };
}

const ymd = (p) => `${p.y}-${String(p.m).padStart(2, "0")}-${String(p.d).padStart(2, "0")}`;

function occursToday(startParts, startDow, rrule, exdates, today) {
  const todayKey = ymd(today);
  if (ymd(startParts) === todayKey) return !exdates.has(todayKey);
  if (!rrule) return false;
  if (exdates.has(todayKey)) return false;

  const startUTC = Date.UTC(startParts.y, startParts.m - 1, startParts.d);
  const todayUTC = Date.UTC(today.y, today.m - 1, today.d);
  if (startUTC > todayUTC) return false;
  const days = Math.round((todayUTC - startUTC) / 86400000);

  const rules = {};
  for (const chunk of rrule.split(";")) {
    const [k, v] = chunk.split("=");
    if (k) rules[k.toUpperCase()] = (v || "").toUpperCase();
  }
  const interval = parseInt(rules.INTERVAL || "1", 10) || 1;

  if (rules.UNTIL) {
    const u = rules.UNTIL.slice(0, 8);
    if (/^\d{8}$/.test(u) && todayKey.replace(/-/g, "") > u) return false;
  }

  switch (rules.FREQ) {
    case "DAILY":
      return days % interval === 0;
    case "WEEKLY": {
      if (Math.floor(days / 7) % interval) return false;
      const byday = (rules.BYDAY || "")
        .split(",").map((d) => WEEKDAYS[d]).filter((d) => d !== undefined);
      return byday.length ? byday.includes(today.dow) : today.dow === startDow;
    }
    case "MONTHLY": {
      const months = (today.y - startParts.y) * 12 + (today.m - startParts.m);
      return months % interval === 0 && today.d === startParts.d;
    }
    case "YEARLY":
      return (today.y - startParts.y) % interval === 0
        && today.m === startParts.m && today.d === startParts.d;
    default:
      return false;
  }
}

function agendaFrom(icsText, tz, now) {
  const today = partsIn(now, tz);
  const rows = [];
  let cur = null;

  for (const line of unfold(icsText)) {
    const upper = line.toUpperCase();
    if (upper.startsWith("BEGIN:VEVENT")) { cur = { exdates: new Set() }; continue; }
    if (upper.startsWith("END:VEVENT")) {
      if (cur && cur.start) {
        const p = cur.start.parts;
        if (occursToday(p, dowOf(p), cur.rrule, cur.exdates, today)) {
          rows.push({
            allDay: !!cur.start.allDay,
            sort: cur.start.allDay ? -1 : Number(p.hh) * 60 + Number(p.mm),
            event: {
              time: cur.start.allDay ? "All day" : `${p.hh}:${p.mm}`,
              title: cur.summary || "(no title)",
              detail: cur.location || "",
            },
          });
        }
      }
      cur = null;
      continue;
    }
    if (!cur || !line.includes(":")) continue;

    const idx = line.indexOf(":");
    const head = line.slice(0, idx);
    const value = line.slice(idx + 1);
    const bits = head.split(";");
    const name = bits[0].toUpperCase();
    const params = {};
    for (const b of bits.slice(1)) {
      const [k, v] = b.split("=");
      params[k.toUpperCase()] = (v || "").replace(/^"|"$/g, "");
    }

    if (name === "DTSTART") cur.start = parseDate(value, params, tz);
    else if (name === "SUMMARY") cur.summary = unescape_(value);
    else if (name === "LOCATION") cur.location = unescape_(value);
    else if (name === "RRULE") cur.rrule = value.trim();
    else if (name === "EXDATE") {
      for (const piece of value.split(",")) {
        const p = parseDate(piece, params, tz);
        if (p) cur.exdates.add(ymd(p.parts));
      }
    } else if (name === "STATUS" && value.trim().toUpperCase() === "CANCELLED") {
      cur.start = null;
    }
  }

  rows.sort((a, b) => a.sort - b.sort);
  const label = new Intl.DateTimeFormat("en-US", {
    timeZone: tz, weekday: "long", month: "long", day: "numeric",
  }).format(now);
  return { date: label, events: rows.slice(0, MAX_EVENTS).map((r) => r.event) };
}

export default {
  async fetch(request, env) {
    const tz = env.TZ_NAME || "America/Denver";
    const doc = { schema: 1, refresh_after_s: 1800 };

    if (env.ICS_URL) {
      try {
        const res = await fetch(env.ICS_URL, {
          headers: { "user-agent": "devday-terminal" },
          cf: { cacheTtl: 300, cacheEverything: true },
        });
        if (res.ok) {
          const agenda = agendaFrom(await res.text(), tz, new Date());
          // Omit an empty agenda entirely: the device merges section by
          // section, so sending an empty one would wipe a good day rather
          // than leave it alone.
          if (agenda.events.length) doc.agenda = agenda;
          doc.date = agenda.date;
        }
      } catch (err) {
        // Serve a valid document regardless — the terminal keeps its last
        // good screen, and an error page would fail schema validation anyway.
      }
    }

    // A string body, so the runtime sets Content-Length. Streaming here would
    // send chunked and the terminal would silently ignore every response.
    const body = JSON.stringify(doc);
    if (body.length > MAX_BYTES) {
      return new Response(JSON.stringify({ schema: 1 }), {
        headers: { "content-type": "application/json" },
      });
    }
    return new Response(body, {
      headers: {
        "content-type": "application/json",
        // Weak validator is fine: the device only checks equality.
        etag: `"${body.length}-${doc.date || "none"}"`,
        "cache-control": "public, max-age=300",
      },
    });
  },
};
