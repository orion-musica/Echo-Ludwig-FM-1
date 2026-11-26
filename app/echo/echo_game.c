/* ECHO LUDWIG - game logic (menus by sound, echo rounds, rewards). */
#include "echo_game.h"
#include "echo_speech.h"
#include "echo_synth.h"
#include "echo_data.h"
#include "echo_fx.h"
#include <string.h>

enum { V_FULL, V_SHORT, V_SOUNDS };
enum { D_EASY, D_NORMAL, D_HARD };
enum { SCR_BOOT, SCR_MAIN, SCR_CRATES, SCR_RECORDS, SCR_GAME, SCR_PAUSE, SCR_JAM, SCR_LIBRARY,
       SCR_EXPLORER, SCR_SETTINGS, SCR_HELP, SCR_CONFIRM };
enum { G_INTRO, G_LISTEN, G_TURN, G_FEEDBACK, G_ROUND_OK, G_COMPLETE, G_DROP, G_OVER };
enum { MAIN_ITEMS = 7, SETTINGS_ITEMS = 13, HELP_PAGES = 8, HOLD_MS = 800, HOLD_MAIN_MS = 2500, PAUSE_ITEMS = 4 };
enum { CTRL_SIMPLE, CTRL_CLASSIC };

/* ------------------------------------------------------------ save data */
#define SAVE_MAGIC 0x4f484345u /* "ECHO" */
typedef struct {
    uint32_t magic; uint16_t version, size;
    uint8_t stars[ECHO_MELODIES];
    uint16_t best[ECHO_MELODIES];
    uint32_t quick_best;
    uint8_t difficulty, voice, speed, verbosity, pan, tempo, beat, screen, music_vol, voice_vol, welcomed, lights, controls;
    uint32_t crc;
} save_t;
static save_t sv;

static uint32_t crc32(const uint8_t *p, unsigned n)
{
    uint32_t c = 0xffffffffu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u))); }
    return ~c;
}
static void defaults(void)
{
    memset(&sv, 0, sizeof sv);
    sv.magic = SAVE_MAGIC; sv.version = 2; sv.size = sizeof sv;
    sv.difficulty = D_NORMAL; sv.voice = SP_VOICE_LUDWIG; sv.speed = SP_NORMAL; sv.verbosity = V_FULL;
    sv.pan = 1; sv.tempo = 1; sv.beat = 1; sv.screen = 1; sv.music_vol = 7; sv.voice_vol = 8; sv.lights = FX_WAVE_FLASH;
}
static void store(void)
{
    sv.crc = crc32((const uint8_t *)&sv, (unsigned)offsetof(save_t, crc));
    echo_hal_save(&sv, sizeof sv);
}
static void apply_settings(void)
{
    sp_set_voice(sv.voice, sv.speed);
    syn_levels((float)sv.music_vol / 10.0f, (float)sv.voice_vol / 9.0f, sv.pan);
    fx_mode(sv.lights);
}

/* --------------------------------------------------------------- state */
static unsigned screen, main_i, crate_i, record_i, lib_i, set_i, help_i, jam_style, explorer_home;
static uint32_t now, prev_buttons, prev_keys, pressed, released, key_down, key_up, home_since, explorer_home_at;
static uint8_t home_held, jam_beat, lib_count, any_stage, pause_i;
static uint32_t any_since, raw_pressed;
static uint8_t lib_list[ECHO_MELODIES];
static uint32_t rng = 0x2545f491u;
static int lit_key = -1;

static struct {
    int mel, quick, phase, played, idle_warned, key_midi, dir;
    unsigned reveal, pos, lives, mistakes, hints, combo, records_done, rounds;
    uint32_t score, t, turn_at;
} g;

/* --------------------------------------------------------- speech text */
static char said[320], text[320];
static unsigned text_len;
static void t_clear(void) { text_len = 0; text[0] = 0; }
static void t_add(const char *s)
{
    if (text_len && text_len < sizeof text - 1 && text[text_len - 1] != ' ') text[text_len++] = ' ';
    while (*s && text_len < sizeof text - 1) text[text_len++] = *s++;
    text[text_len] = 0;
}
static void t_num(unsigned v)
{
    char d[12]; int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    char s[12]; for (int i = 0; i < n; ++i) s[i] = d[n - 1 - i]; s[n] = 0;
    t_add(s);
}
static void say(const char *s)
{
    size_t n = strlen(s); if (n >= sizeof said) n = sizeof said - 1;
    memcpy(said, s, n); said[n] = 0;
    sp_say(s);
}
static void say_more(const char *s)
{
    size_t have = strlen(said), n = strlen(s);
    if (have + n + 2 < sizeof said) { said[have] = ' '; memcpy(said + have + 1, s, n + 1); }
    sp_queue(s);
}
static void say_text(void) { say(text); }
static void say_more_text(void) { say_more(text); }
static int talk(unsigned level) { return sv.verbosity <= level; } /* talk(V_SHORT): full or short */
static int quiet(void) { return !sp_busy() && !syn_playing() && !syn_earcon_busy(); }

/* --------------------------------------------------------------- screen */
static unsigned msg_icon, msg_serial;
static char line1[40], line2[40];
static void copy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    for (; src && src[i] && i < cap - 1; ++i) { char c = src[i]; dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
    dst[i] = 0;
}
static void show(unsigned icon, const char *a, const char *b)
{
    msg_icon = icon; copy(line1, a, sizeof line1); copy(line2, b, sizeof line2); ++msg_serial;
}
static char *u2a(char *p, unsigned v)
{
    char d[12]; int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = d[--n];
    *p = 0;
    return p;
}
static char *s2a(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
void echo_game_message(unsigned *icon, const char **a, const char **b, unsigned *serial)
{
    static const char empty[1] = "";
    *serial = msg_serial + (sv.screen ? 0u : 0x8000u);
    if (!sv.screen) { *icon = IC_NONE; *a = *b = empty; return; }
    *icon = msg_icon; *a = line1; *b = line2;
}
static void led_key(int key)
{
    if (lit_key == key) return;
    if (lit_key >= 0) fx_base(14u + (unsigned)lit_key, 0);
    if (key >= 0 && key < KEY_COUNT) fx_base(14u + (unsigned)key, 1);
    lit_key = key;
}

/* ------------------------------------------------------------- helpers */
static unsigned rnd(unsigned n)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return n ? rng % n : 0;
}
static int wrap(int v, int n) { while (v < 0) v += n; while (v >= n) v -= n; return v; }
static unsigned crate_records(unsigned c, unsigned *stars_out)
{
    unsigned done = 0, stars = 0;
    for (unsigned i = 0; i < ECHO_MELODIES; ++i)
        if (echo_melodies[i].crate == c) { stars += sv.stars[i]; if (sv.stars[i]) ++done; }
    if (stars_out) *stars_out = stars;
    return done;
}
static int crate_open(unsigned c) { return c == 0 || crate_records(c - 1, NULL) >= 3; }
static unsigned total_stars(void)
{
    unsigned s = 0;
    for (unsigned i = 0; i < ECHO_MELODIES; ++i) s += sv.stars[i];
    return s;
}
static const char *const rank_say[5] = {"beginner", "apprentice", "musician", "soloist", "maestro"};
static const char *const rank_show[5] = {"BEGINNER", "APPRENTICE", "MUSICIAN", "SOLOIST", "MAESTRO"};
static unsigned rank_of(unsigned stars)
{
    return stars >= 65 ? 4 : stars >= 40 ? 3 : stars >= 20 ? 2 : stars >= 8 ? 1 : 0;
}
static int record_in_crate(unsigned crate, unsigned n) /* n-th record of a crate -> melody index */
{
    for (unsigned i = 0; i < ECHO_MELODIES; ++i)
        if (echo_melodies[i].crate == crate && n-- == 0) return (int)i;
    return -1;
}
static unsigned crate_size(unsigned crate)
{
    unsigned n = 0;
    for (unsigned i = 0; i < ECHO_MELODIES; ++i) n += echo_melodies[i].crate == crate;
    return n;
}
static int nav(const echo_input *in)
{
    int d = in->enc[0] + in->enc[6];
    if (pressed & (1u << B_OCT_UP)) ++d;
    if (pressed & (1u << B_OCT_DOWN)) --d;
    return d;
}
static int lowest_key(uint32_t bits);
static int btn(unsigned b) { return (pressed >> b) & 1u; }
static int simple(void) { return sv.controls == CTRL_SIMPLE; }
static const char *menu_help(void) { return echo_phrases[simple() ? P_HELP_MENU : P_HELP_MENU_CLASSIC]; }
/* Keys on the left half (F3..F4) and the right half (F#4..G5) of the keybed. */
static int key_side(void)
{
    int k = lowest_key(key_down);
    return k < 0 ? 0 : k < 13 ? -1 : 1;
}
static int lowest_key(uint32_t bits)
{
    for (int k = 0; k < KEY_COUNT; ++k) if (bits & (1u << k)) return k;
    return -1;
}
static void stars_text(char *p, unsigned stars)
{
    for (unsigned i = 0; i < 3; ++i) { *p++ = i < stars ? '*' : '-'; *p++ = ' '; }
    p[-1] = 0;
}
static void silence(void) { syn_stop(); syn_note_off(); led_key(-1); }

/* ---------------------------------------------------------------- menus */
static const char *const main_say[MAIN_ITEMS] = {
    "play", "quick echo", "jam", "library", "key explorer", "settings", "help"};
static const char *const main_show[MAIN_ITEMS] = {
    "PLAY", "QUICK ECHO", "JAM", "LIBRARY", "KEY EXPLORER", "SETTINGS", "HELP"};
static const uint8_t main_icon[MAIN_ITEMS] = {IC_PLAY, IC_BOLT, IC_KEYS, IC_DISC, IC_EAR, IC_GEAR, IC_HELP};

static void tick_sound(unsigned index, unsigned count, int wrapped)
{
    syn_earcon(wrapped ? EAR_WRAP : EAR_TICK, count > 1 ? index * 12u / (count - 1) : 0);
}
static void announce_main(void)
{
    char b[24], *p = u2a(b, main_i + 1); p = s2a(p, " / "); u2a(p, MAIN_ITEMS);
    show(main_icon[main_i], main_show[main_i], b);
    say(main_say[main_i]);
}
static void enter_main(int title)
{
    screen = SCR_MAIN; silence();
    if (title) { say(echo_phrases[P_MENU_MAIN]); say_more(main_say[main_i]); }
    else announce_main();
    char b[24], *p = u2a(b, main_i + 1); p = s2a(p, " / "); u2a(p, MAIN_ITEMS);
    show(main_icon[main_i], main_show[main_i], b);
}
static uint8_t crate_queue;
static void announce_crate(void)
{
    const echo_crate_info *c = &echo_crates[crate_i];
    unsigned stars, done = crate_records(crate_i, &stars), size = crate_size(crate_i);
    char b[32], *p;
    t_clear(); t_add("crate"); t_num(crate_i + 1); t_add(c->say); t_add(".");
    if (crate_open(crate_i)) {
        t_num(done); t_add("of"); t_num(size); t_add("records.");
        p = s2a(b, "STARS "); p = u2a(p, stars); p = s2a(p, " / "); u2a(p, size * 3);
        show(IC_CRATE, c->title, b);
    } else {
        t_add(echo_phrases[P_LOCKED]);
        show(IC_LOCK, c->title, "LOCKED");
        syn_earcon(EAR_LOCKED, 0);
    }
    if (crate_queue) say_more_text(); else say_text();
}
static void announce_record(void)
{
    int m = record_in_crate(crate_i, record_i);
    if (m < 0) return;
    const echo_melody *e = &echo_melodies[m];
    char b[40], *p = s2a(b, e->composer); p = s2a(p, "  ");
    t_clear(); t_add(e->say); t_add(","); t_add(e->saycomposer); t_add(".");
    if (sv.stars[m]) { t_num(sv.stars[m]); t_add(sv.stars[m] == 1 ? "star." : "stars."); }
    else t_add("new.");
    stars_text(p, sv.stars[m]);
    show(IC_DISC, e->title, b);
    say_text();
}
static void enter_crates(void)
{
    screen = SCR_CRATES; silence();
    unsigned st = total_stars();
    t_clear(); t_add("rank"); t_add(rank_say[rank_of(st)]); t_add(","); t_num(st); t_add(st == 1 ? "star." : "stars.");
    say_text();
    crate_queue = 1; announce_crate(); crate_queue = 0;
}
static void enter_records(void) { screen = SCR_RECORDS; silence(); announce_record(); }

/* ----------------------------------------------------------------- game */
static const echo_melody *mel(void) { return &echo_melodies[g.mel]; }
static unsigned listen_bpm(void)
{
    static const uint8_t pct[3] = {65, 85, 100};
    return echo_crates[mel()->crate].bpm * pct[sv.tempo % 3] / 100u;
}
static void game_status_line(char *b)
{
    char *p = s2a(b, "NOTE "); p = u2a(p, g.pos + 1 > g.reveal ? g.reveal : g.pos + 1);
    p = s2a(p, "/"); p = u2a(p, g.reveal); p = s2a(p, "   ");
    if (sv.difficulty == D_EASY) { s2a(p, "EASY"); return; }
    for (unsigned i = 0; i < 3; ++i) *p++ = i < g.lives ? '<' : '.';
    *p = 0;
}
static void start_listen(void)
{
    g.phase = G_LISTEN; g.played = 0; g.pos = 0;
    char b[40], *p = s2a(b, "ROUND "); p = u2a(p, g.rounds + 1); p = s2a(p, "   NOTES "); u2a(p, g.reveal);
    show(IC_EAR, "LISTEN", b);
    if (talk(V_FULL)) say(echo_phrases[P_LISTEN]);
}
static void start_round_game(int mel_index)
{
    g.mel = mel_index;
    g.reveal = mel()->reveal < mel()->count ? mel()->reveal : mel()->count;
    g.mistakes = g.hints = g.combo = g.rounds = 0;
    if (!g.quick) { g.lives = 3; g.score = 0; }
    syn_style(mel()->crate);
    g.phase = G_INTRO;
    show(IC_DISC, mel()->title, mel()->composer);
    t_clear(); t_add(mel()->say); t_add(", by"); t_add(mel()->saycomposer); t_add(".");
    if (talk(V_SHORT)) say_text(); else syn_earcon(EAR_ENTER, 0);
}
static void enter_game(int mel_index, int quick)
{
    screen = SCR_GAME; silence();
    memset(&g, 0, sizeof g);
    g.quick = quick; g.lives = 3; g.key_midi = -1;
    start_round_game(mel_index);
    if (quick) { t_clear(); t_add(echo_phrases[P_QUICK_INTRO]); t_add(mel()->say); t_add("."); say_text(); }
}
static int quick_pick(void)
{
    unsigned open_count = 0, list[ECHO_MELODIES];
    for (unsigned i = 0; i < ECHO_MELODIES; ++i)
        if (crate_open(echo_melodies[i].crate) && (int)i != g.mel) list[open_count++] = i;
    return open_count ? (int)list[rnd(open_count)] : 0;
}
static void start_turn(void)
{
    g.phase = G_TURN; g.pos = 0; g.turn_at = now; g.idle_warned = 0;
    syn_earcon(EAR_YOUR_TURN, 0);
    if (talk(V_FULL) && g.rounds == 0) say(echo_phrases[P_YOUR_TURN]);
    char b[40]; game_status_line(b);
    show(IC_KEYS, "YOUR TURN", b);
}
static void say_status(void)
{
    t_clear(); t_add("score"); t_num(g.score); t_add(". round"); t_num(g.rounds + 1); t_add(".");
    if (sv.difficulty != D_EASY) { t_num(g.lives); t_add(g.lives == 1 ? "life left." : "lives left."); }
    if (g.combo > 1) { t_add("combo"); t_num(g.combo); t_add("."); }
    say_text();
    char b[40], *p = s2a(b, "SCORE "); u2a(p, g.score);
    show(IC_BOLT, b, "");
}
static void complete_record(void)
{
    unsigned before_rank = rank_of(total_stars());
    unsigned next_crate = mel()->crate + 1, was_open = next_crate < ECHO_CRATES ? crate_open(next_crate) : 1;
    unsigned errors = g.mistakes + g.hints;
    unsigned stars = errors == 0 ? 3 : errors <= 2 ? 2 : 1;
    int is_new = sv.stars[g.mel] == 0;
    g.score += 100 + (stars == 3 ? 200 : 0);
    if (stars > sv.stars[g.mel]) sv.stars[g.mel] = (uint8_t)stars;
    if (!g.quick && g.score > sv.best[g.mel]) sv.best[g.mel] = (uint16_t)(g.score > 65535 ? 65535 : g.score);
    store();
    syn_earcon(EAR_UNLOCK, 0);
    char b[40]; stars_text(b, stars);
    show(IC_STAR, is_new ? "UNLOCKED" : "COMPLETE", b);
    t_clear();
    if (is_new) t_add(echo_phrases[P_UNLOCKED]); else t_add("complete!");
    t_num(stars); t_add(stars == 1 ? "star." : "stars.");
    if (stars == 3) t_add(echo_phrases[P_PERFECT]);
    if (!talk(V_SOUNDS)) t_clear();
    unsigned after_rank = rank_of(total_stars());
    if (after_rank > before_rank) { t_add(echo_phrases[P_NEW_RANK]); t_add(rank_say[after_rank]); t_add("!"); }
    if (!was_open && next_crate < ECHO_CRATES && crate_open(next_crate)) {
        t_add(echo_phrases[P_CRATE_UNLOCKED]); t_add(echo_crates[next_crate].say); t_add(".");
    }
    if (text_len) say_text();
    g.phase = G_COMPLETE;
}
static void game_over(void)
{
    silence();
    syn_earcon(EAR_GAME_OVER, 0);
    g.phase = G_OVER;
    if (g.quick) {
        t_clear(); t_add(echo_phrases[P_GAME_OVER]); t_add("score"); t_num(g.score); t_add(".");
        if (g.score > sv.quick_best) { sv.quick_best = g.score; store(); t_add(echo_phrases[P_HIGH_SCORE]); }
        t_add(echo_phrases[P_OVER_CHOICE]);
        char b[40], *p = s2a(b, "SCORE "); u2a(p, g.score);
        show(IC_CROSS, "GAME OVER", b);
        say_text();
    } else {
        show(IC_CROSS, "SCRATCHED", "RIGHT: RETRY  LEFT: BACK");
        say(echo_phrases[P_SCRATCHED]);
    }
}
static void game_key(int key)
{
    unsigned midi = (unsigned)(KEY_LOWEST_MIDI + key);
    syn_note_on(midi);
    g.key_midi = (int)midi;
    led_key(key);
    g.turn_at = now; g.idle_warned = 0;
    unsigned want = mel()->notes[g.pos * 2];
    if (midi == want) {
        ++g.pos; ++g.combo;
        unsigned mult = 1 + g.combo / 8; if (mult > 4) mult = 4;
        g.score += 10 * mult;
        if (g.combo && g.combo % 8 == 0) syn_earcon(EAR_COMBO, (g.combo / 8) * 2);
        char b[40]; game_status_line(b);
        show(IC_CHECK, "YOUR TURN", b);
        if (g.pos >= g.reveal) { g.phase = G_ROUND_OK; g.t = now + 380; }
    } else {
        ++g.mistakes; g.combo = 0;
        g.dir = want > midi ? 1 : -1;
        g.phase = G_FEEDBACK; g.t = now + 320;
        show(IC_CROSS, g.dir > 0 ? "HIGHER" : "LOWER", "");
    }
}
static const char *const pause_say[PAUSE_ITEMS] = {"go on", "hint", "score", "quit"};
static const char *const pause_show[PAUSE_ITEMS] = {"GO ON", "HINT", "SCORE", "QUIT"};
static void announce_pause(void)
{
    char b[24], *p = u2a(b, pause_i + 1); p = s2a(p, " / "); u2a(p, PAUSE_ITEMS);
    show(IC_HELP, pause_show[pause_i], b);
    say(pause_say[pause_i]);
}
static void enter_pause(void)
{
    silence(); screen = SCR_PAUSE; pause_i = 0;
    show(IC_HELP, "PAUSE", "GO ON");
    say(echo_phrases[P_PAUSED]); say_more(pause_say[0]);
}
static void play_hint(void)
{
    if (sv.difficulty == D_HARD) { say(echo_phrases[P_NO_HINT]); return; }
    ++g.hints; g.combo = 0;
    uint8_t one[2] = {mel()->notes[g.pos * 2], 12};
    syn_play(one, 1, 100, 0, SEQ_LEAD, 1);
    show(IC_HELP, "HINT", "");
}
static void game_tick(const echo_input *in)
{
    /* buttons that work in every phase */
    if (btn(B_HOME)) { enter_pause(); return; }
    if (btn(B_ARP)) { say_status(); return; }
    (void)in;
    switch (g.phase) {
    case G_INTRO:
        if (quiet()) start_listen();
        break;
    case G_LISTEN:
        if (!g.played) {
            if (!sp_busy()) {
                syn_play(mel()->notes, g.reveal, listen_bpm(), mel()->root, SEQ_LEAD | (sv.beat ? SEQ_HATS : 0), 1);
                g.played = 1;
            }
        } else {
            int n = syn_note_index();
            led_key(n >= 0 ? (int)mel()->notes[n * 2] - KEY_LOWEST_MIDI : -1);
            if (!syn_playing()) { led_key(-1); start_turn(); }
        }
        break;
    case G_TURN: {
        if (key_up && g.key_midi >= 0 && (key_up & (1u << (g.key_midi - KEY_LOWEST_MIDI)))) { syn_note_off(); g.key_midi = -1; led_key(-1); }
        int k = lowest_key(key_down);
        if (k >= 0) { game_key(k); break; }
        if (btn(B_PLAY)) { silence(); start_listen(); break; }
        if (btn(B_SEQ)) { play_hint(); break; }
        if (!g.idle_warned && now - g.turn_at > 10000u) {
            g.idle_warned = 1;
            if (talk(V_SHORT)) say(simple() ? echo_phrases[P_IDLE_TURN] : "your turn. press play to listen again.");
            else syn_earcon(EAR_YOUR_TURN, 0);
        }
        break;
    }
    case G_FEEDBACK:
        if ((int32_t)(now - g.t) < 0) break;
        syn_note_off(); g.key_midi = -1; led_key(-1);
        if (sv.difficulty != D_HARD) {
            syn_earcon(g.dir > 0 ? EAR_HIGHER : EAR_LOWER, 0);
            if (talk(V_SHORT)) say(echo_phrases[g.dir > 0 ? P_HIGHER : P_LOWER]);
        } else syn_earcon(EAR_WRONG, 0);
        if (sv.difficulty == D_EASY) { g.phase = G_TURN; g.turn_at = now; break; }
        --g.lives;
        if (g.lives == 0) { game_over(); break; }
        syn_earcon(EAR_LIFE, 0);
        if (talk(V_SHORT)) {
            if (g.lives == 1) say_more(echo_phrases[P_LAST_LIFE]);
            else { t_clear(); t_num(g.lives); t_add("lives left."); say_more_text(); }
        }
        start_listen();
        if (talk(V_FULL)) say_more(echo_phrases[P_LISTEN]);
        break;
    case G_ROUND_OK:
        if ((int32_t)(now - g.t) < 0) break;
        syn_note_off(); g.key_midi = -1; led_key(-1);
        ++g.rounds; g.score += 50;
        if (g.reveal >= mel()->count) { complete_record(); break; }
        syn_earcon(EAR_ROUND_OK, 0);
        {
            unsigned step = mel()->count > 10 ? 2 : 1;
            g.reveal = g.reveal + step > mel()->count ? mel()->count : g.reveal + step;
        }
        if (talk(V_SHORT)) {
            t_clear(); t_add(echo_phrases[P_PRAISE_0 + rnd(5)]);
            if (g.reveal == mel()->count && talk(V_FULL)) t_add(echo_phrases[P_ALMOST]);
            say_text();
        }
        show(IC_CHECK, "ROUND CLEAR", "");
        g.phase = G_INTRO; /* waits for quiet, then listens again */
        break;
    case G_COMPLETE:
        if (!quiet()) break;
        g.phase = G_DROP;
        syn_style(mel()->crate);
        syn_play(mel()->notes, mel()->count, echo_crates[mel()->crate].bpm, mel()->root, SEQ_LEAD | SEQ_FULL, g.quick ? 1 : 2);
        {
            char b[40]; stars_text(b, sv.stars[g.mel]);
            show(IC_STAR, mel()->title, b);
        }
        break;
    case G_DROP: {
        int n = syn_note_index();
        led_key(n >= 0 ? (int)mel()->notes[n * 2] - KEY_LOWEST_MIDI : -1);
        if (syn_playing() && !pressed && !key_down) break;
        silence();
        if (g.quick) { ++g.records_done; start_round_game(quick_pick()); }
        else enter_records();
        break;
    }
    case G_OVER: {
        /* two choices you can find by touch: right half = try again, left half = back */
        int side = key_side();
        if (btn(B_PLAY) || side > 0) {
            if (g.quick) enter_game(quick_pick(), 1);
            else enter_game(g.mel, 0);
        } else if (side < 0) {
            syn_earcon(EAR_BACK, 0);
            if (g.quick) enter_main(1); else enter_records();
        }
        break;
    }
    }
}
static void resume_game(void) { screen = SCR_GAME; sp_stop(); syn_earcon(EAR_ENTER, 0); }
static void pause_tick(int d)
{
    if (d) { int n = (int)pause_i + d; pause_i = (uint8_t)wrap(n, PAUSE_ITEMS); tick_sound(pause_i, PAUSE_ITEMS, n != (int)pause_i); announce_pause(); return; }
    if (btn(B_HOME)) { resume_game(); start_listen(); return; }   /* back = back to the game */
    if (!btn(B_PLAY)) return;
    switch (pause_i) {
    case 0: resume_game(); start_listen(); break;
    case 1: resume_game(); if (g.phase == G_TURN) play_hint(); else start_listen(); break;
    case 2: say_status(); break;
    default: syn_earcon(EAR_BACK, 0); if (g.quick) enter_main(1); else enter_records(); break;
    }
}

/* ------------------------------------------------------------ settings */
static uint8_t *setting_value(unsigned i)
{
    uint8_t *v[SETTINGS_ITEMS] = {&sv.difficulty, &sv.voice, &sv.speed, &sv.verbosity, &sv.pan, &sv.tempo,
                                  &sv.beat, &sv.screen, &sv.music_vol, &sv.voice_vol, &sv.lights, &sv.controls, NULL};
    return v[i];
}
static const uint8_t setting_count[SETTINGS_ITEMS] = {3, 2, 4, 3, 2, 3, 2, 2, 11, 10, 3, 2, 1};
static const char *const setting_say[SETTINGS_ITEMS] = {
    "difficulty", "voice", "speech speed", "talk", "pitch panning", "listen tempo", "beat while listening",
    "screen", "music volume", "voice volume", "lights", "controls", "reset progress"};
static const char *const setting_show[SETTINGS_ITEMS] = {
    "DIFFICULTY", "VOICE", "SPEECH SPEED", "TALK", "PITCH PANNING", "LISTEN TEMPO", "LISTEN BEAT",
    "SCREEN", "MUSIC VOLUME", "VOICE VOLUME", "LIGHTS", "CONTROLS", "RESET PROGRESS"};
static const char *value_name(unsigned item, unsigned v)
{
    static const char *const names[8][4] = {
        {"easy", "normal", "hard"}, {"ludwig", "clara"}, {"slow", "normal", "fast", "very fast"},
        {"full", "short", "sounds only"}, {"off", "on"}, {"slow", "normal", "original"}, {"off", "on"},
        {"off", "minimal"}};
    static const char *const lights[3] = {"off", "wave", "wave and flash"};
    if (item == 10) return lights[v % 3];
    if (item == 11) return v ? "classic" : "simple";
    return item < 8 ? names[item][v] : NULL;
}
static void announce_setting(int value_only)
{
    uint8_t *v = setting_value(set_i);
    char b[40];
    t_clear();
    if (!value_only) t_add(setting_say[set_i]);
    if (v) {
        const char *n = value_name(set_i, *v);
        if (n) { t_add(n); copy(b, n, sizeof b); } else { t_num(*v); u2a(b, *v); }
    } else copy(b, "PRESS PLAY", sizeof b);
    show(IC_GEAR, setting_show[set_i], b);
    say_text();
}
static void change_setting(int delta)
{
    uint8_t *v = setting_value(set_i);
    if (!v) { screen = SCR_CONFIRM; show(IC_LOCK, "RESET PROGRESS?", "RIGHT: YES  LEFT: NO"); say(echo_phrases[P_RESET_SURE]); return; }
    *v = (uint8_t)wrap((int)*v + delta, setting_count[set_i]);
    apply_settings();
    store();
    syn_earcon(EAR_TICK, *v * 3u);
    announce_setting(1);
}

/* ------------------------------------------------------------ explorer */
static const char *const note_say[12] = {"C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A", "A sharp", "B"};
static const char *const note_show[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void explorer_tick(void)
{
    if (key_up && g.key_midi >= 0 && (key_up & (1u << (g.key_midi - KEY_LOWEST_MIDI)))) { syn_note_off(); g.key_midi = -1; led_key(-1); }
    int k = lowest_key(key_down);
    if (k >= 0) {
        unsigned midi = (unsigned)(KEY_LOWEST_MIDI + k), octave = midi / 12 - 1;
        syn_note_on(midi); g.key_midi = (int)midi; led_key(k);
        char b[40], *p = s2a(b, note_show[midi % 12]); p = u2a(p, octave); p = s2a(p, "   KEY "); u2a(p, (unsigned)k + 1);
        t_clear();
        if (midi == 60) t_add("middle C"); else { t_add(note_say[midi % 12]); t_num(octave); }
        show(IC_NOTE, midi == 60 ? "MIDDLE C" : b, midi == 60 ? "C4   KEY 8" : "");
        say_text();
        return;
    }
    if (simple() && btn(B_HOME)) { syn_earcon(EAR_BACK, 0); enter_main(1); return; }   /* held button */
    for (unsigned b = 0; b < B_COUNT; ++b) {
        if (!((simple() ? raw_pressed : pressed) & (1u << b))) continue;
        if (!simple() && b == B_HOME) {
            if (explorer_home && now - explorer_home_at < 1500u) { enter_main(1); return; }
            explorer_home = 1; explorer_home_at = now;
        }
        static const char *const names[B_COUNT] = {"OCT -", "OCT +", "FX", "SCL", "ENV", "LFO", "EDIT", "GLO",
                                                   "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
        show(IC_HELP, names[b], "BUTTON");
        say(echo_phrases[P_BTN_0 + b]);
        return;
    }
}

/* -------------------------------------------------------------- library */
static void build_library(void)
{
    lib_count = 0;
    for (unsigned i = 0; i < ECHO_MELODIES; ++i) if (sv.stars[i]) lib_list[lib_count++] = (uint8_t)i;
}
static void announce_library(void)
{
    const echo_melody *e = &echo_melodies[lib_list[lib_i]];
    char b[40], *p = s2a(b, e->composer); p = s2a(p, " "); u2a(p, e->year);
    t_clear(); t_add(e->say); t_add(","); t_add(e->saycomposer); t_num(e->year); t_add(".");
    show(IC_DISC, e->title, b);
    say_text();
}

/* ----------------------------------------------------------------- jam */
static void jam_start_beat(void)
{
    syn_style(jam_style);
    syn_play(NULL, 0, echo_crates[jam_style].bpm, 0, SEQ_FULL | SEQ_LOOP, 1);
    jam_beat = 1;
}
static void announce_jam(void)
{
    show(IC_KEYS, "JAM", echo_crates[jam_style].title);
    say(echo_crates[jam_style].say);
}

/* ---------------------------------------------------------------- help */
static void announce_help(void)
{
    char b[24], *p = s2a(b, "PAGE "); p = u2a(p, help_i + 1); p = s2a(p, " / "); u2a(p, HELP_PAGES);
    show(IC_HELP, "HELP", b);
    say(echo_phrases[P_HELP_1 + help_i]);
}

/* -------------------------------------------------------- global keys */
static void screen_help(void)
{
    switch (screen) {
    case SCR_GAME: case SCR_PAUSE: say(echo_phrases[simple() ? P_GAME_HELP : P_GAME_HELP_CLASSIC]); break;
    case SCR_JAM: say(echo_phrases[P_JAM_INTRO]); break;
    case SCR_EXPLORER: say(echo_phrases[P_EXPLORER_INTRO]); break;
    default: say(menu_help()); break;
    }
}
static int global_buttons(void)
{
    if (screen == SCR_EXPLORER || simple()) return 0;   /* simple controls: every button is the same */
    if (btn(B_REC)) { say(said[0] ? said : menu_help()); return 1; }
    if (btn(B_EDIT)) { screen_help(); return 1; }
    if (btn(B_SCL)) { sv.speed = (uint8_t)wrap(sv.speed + 1, 4); apply_settings(); store();
                      t_clear(); t_add("speed"); t_add(value_name(2, sv.speed)); say_text(); return 1; }
    if (btn(B_ENV)) { sv.verbosity = (uint8_t)wrap(sv.verbosity + 1, 3); store();
                      t_clear(); t_add("talk"); t_add(value_name(3, sv.verbosity)); say_text(); return 1; }
    if (btn(B_GLO)) { sv.voice ^= 1u; apply_settings(); store();
                      t_clear(); t_add("voice"); t_add(value_name(1, sv.voice)); say_text(); return 1; }
    if (btn(B_FX) && screen != SCR_JAM) { sv.beat ^= 1u; store();
                      t_clear(); t_add("beat"); t_add(sv.beat ? "on" : "off"); say_text(); return 1; }
    if (btn(B_SAVE)) { store(); say(echo_phrases[P_SAVED]); return 1; }
    return 0;
}

/* -------------------------------------------------------------- public */
#ifdef ECHO_HOST_DEBUG
int echo_debug_expected(void)
{
    if (screen != SCR_GAME || g.phase != G_TURN) return -1;
    return mel()->notes[g.pos * 2];
}
int echo_debug_screen(void) { return (int)screen; }
#endif
void echo_game_init(uint32_t t)
{
    now = t;
    defaults();
    save_t loaded;
    if (echo_hal_load(&loaded, sizeof loaded) == 0 && loaded.magic == SAVE_MAGIC && loaded.size == sizeof loaded &&
        loaded.crc == crc32((const uint8_t *)&loaded, (unsigned)offsetof(save_t, crc)))
        sv = loaded;
    sp_init(); syn_init(); fx_init(); apply_settings();
    screen = SCR_BOOT;
    show(IC_NOTE, "ECHO LUDWIG", "A LISTENING GAME");
    syn_earcon(EAR_UNLOCK, 0);
    say(echo_phrases[P_BOOT]);
    if (!sv.welcomed) { say_more(echo_phrases[P_HELP_1]); say_more(menu_help()); sv.welcomed = 1; store(); }
    prev_buttons = prev_keys = 0;
}

void echo_game_tick(uint32_t t, const echo_input *in)
{
    now = t;
    rng += t;
    pressed = in->buttons & ~prev_buttons; released = prev_buttons & ~in->buttons;
    key_down = in->keys & ~prev_keys; key_up = prev_keys & ~in->keys;
    prev_buttons = in->buttons; prev_keys = in->keys;
    sp_pump();
    /* light wave + flash from every new press */
    for (unsigned b = 0; b < B_COUNT; ++b) if (pressed & (1u << b)) fx_press(b, now);
    for (unsigned k = 0; k < KEY_COUNT; ++k) if (key_down & (1u << k)) fx_press(14u + k, now);
    fx_tick(now);

    raw_pressed = pressed;
    if (simple()) {
        /* Simple controls: every button is the same button.
         * short press = select (PLAY), hold = back (HOME), long hold = main menu. */
        static uint32_t prev_any;
        uint32_t any = in->buttons != 0;
        pressed = 0;
        if (any && !prev_any) { any_since = now; any_stage = 0; }
        if (any) {
            if (any_stage == 0 && now - any_since >= HOLD_MS) { any_stage = 1; pressed = 1u << B_HOME; syn_earcon(EAR_BACK, 0); }
            else if (any_stage == 1 && now - any_since >= HOLD_MAIN_MS) {
                any_stage = 2;
                if (screen != SCR_MAIN && screen != SCR_BOOT) { enter_main(1); prev_any = any; return; }
            }
        } else if (prev_any && any_stage == 0) pressed = 1u << B_PLAY;
        prev_any = any;
        goto input_done;
    }
    /* HOME: short press = back (handled per screen), hold = main menu */
    if (pressed & (1u << B_HOME)) { home_since = now; home_held = 0; }
    if ((in->buttons & (1u << B_HOME)) && !home_held && now - home_since >= HOLD_MS) {
        home_held = 1;
        if (screen != SCR_MAIN && screen != SCR_BOOT) { enter_main(1); return; }
    }
    if (home_held) pressed &= ~(1u << B_HOME);
    /* a short HOME acts on release, so a hold never also means "back" */
    if (pressed & (1u << B_HOME)) pressed &= ~(1u << B_HOME);
    if ((released & (1u << B_HOME)) && !home_held) pressed |= 1u << B_HOME;
    if (released & (1u << B_HOME)) home_held = 0;
input_done:
    if (global_buttons()) { sp_pump(); return; }
    int d = nav(in);
    d += in->enc[1] && screen != SCR_SETTINGS ? in->enc[1] : 0;           /* any knob moves */
    for (unsigned e = 2; e < 6; ++e) d += in->enc[e];
    /* keys move through lists: left half = previous, right half = next */
    if (screen == SCR_MAIN || screen == SCR_CRATES || screen == SCR_RECORDS || screen == SCR_LIBRARY ||
        screen == SCR_SETTINGS || screen == SCR_HELP || screen == SCR_PAUSE)
        d += key_side();

    switch (screen) {
    case SCR_BOOT:
        if (pressed || key_down || d) { sp_stop(); enter_main(0); }
        else if (quiet()) enter_main(0);
        break;
    case SCR_MAIN:
        if (d) { int n = (int)main_i + d; main_i = (unsigned)wrap(n, MAIN_ITEMS); tick_sound(main_i, MAIN_ITEMS, n != (int)main_i); announce_main(); }
        else if (btn(B_PLAY)) {
            syn_earcon(EAR_ENTER, 0);
            switch (main_i) {
            case 0: enter_crates(); break;
            case 1: enter_game(quick_pick(), 1); break;
            case 2: screen = SCR_JAM; silence(); jam_style = 0; say(echo_phrases[P_JAM_INTRO]); show(IC_KEYS, "JAM", echo_crates[0].title); jam_start_beat(); break;
            case 3: build_library();
                    if (!lib_count) { show(IC_DISC, "LIBRARY", "EMPTY"); say(echo_phrases[P_LIB_EMPTY]); }
                    else { screen = SCR_LIBRARY; lib_i = 0; announce_library(); }
                    break;
            case 4: screen = SCR_EXPLORER; silence(); explorer_home = 0; memset(&g, 0, sizeof g); g.key_midi = -1;
                    show(IC_EAR, "KEY EXPLORER", "PRESS ANY KEY"); say(echo_phrases[P_EXPLORER_INTRO]); break;
            case 5: screen = SCR_SETTINGS; set_i = 0; announce_setting(0); break;
            case 6: screen = SCR_HELP; help_i = 0; announce_help(); break;
            }
        } else if (btn(B_ARP)) {
            unsigned s = total_stars();
            t_clear(); t_add("rank"); t_add(rank_say[rank_of(s)]); t_add("."); t_num(s); t_add("stars.");
            say_text(); show(IC_STAR, rank_show[rank_of(s)], "");
        }
        break;
    case SCR_CRATES:
        if (d) { int n = (int)crate_i + d; crate_i = (unsigned)wrap(n, ECHO_CRATES); tick_sound(crate_i, ECHO_CRATES, n != (int)crate_i); announce_crate(); }
        else if (btn(B_PLAY)) {
            if (crate_open(crate_i)) { syn_earcon(EAR_ENTER, 0); record_i = 0; enter_records(); }
            else { syn_earcon(EAR_LOCKED, 0); say(echo_phrases[P_CRATE_HOW]); }
        } else if (btn(B_HOME)) { syn_earcon(EAR_BACK, 0); enter_main(1); }
        break;
    case SCR_RECORDS: {
        unsigned size = crate_size(crate_i);
        if (d) { int n = (int)record_i + d; record_i = (unsigned)wrap(n, (int)size); tick_sound(record_i, size, n != (int)record_i); announce_record(); }
        else if (btn(B_PLAY)) { syn_earcon(EAR_ENTER, 0); enter_game(record_in_crate(crate_i, record_i), 0); }
        else if (btn(B_HOME)) { syn_earcon(EAR_BACK, 0); enter_crates(); }
        break;
    }
    case SCR_GAME: game_tick(in); break;
    case SCR_PAUSE: pause_tick(d); break;
    case SCR_JAM:
        if (key_up && g.key_midi >= 0 && (key_up & (1u << (g.key_midi - KEY_LOWEST_MIDI)))) { syn_note_off(); g.key_midi = -1; led_key(-1); }
        { int k = lowest_key(key_down);
          if (k >= 0) { syn_note_on((unsigned)(KEY_LOWEST_MIDI + k)); g.key_midi = KEY_LOWEST_MIDI + k; led_key(k); } }
        if (d) { jam_style = (unsigned)wrap((int)jam_style + d, ECHO_CRATES); if (jam_beat) jam_start_beat(); else syn_style(jam_style); announce_jam(); }
        else if (btn(B_PLAY) || btn(B_FX)) { if (jam_beat) { syn_stop(); jam_beat = 0; } else jam_start_beat(); }
        else if (btn(B_HOME)) { silence(); syn_earcon(EAR_BACK, 0); enter_main(1); }
        break;
    case SCR_LIBRARY:
        if (d) { int n = (int)lib_i + d; lib_i = (unsigned)wrap(n, lib_count); silence(); tick_sound(lib_i, lib_count, n != (int)lib_i); announce_library(); }
        else if (btn(B_PLAY)) {
            if (syn_playing()) silence();
            else { const echo_melody *e = &echo_melodies[lib_list[lib_i]]; sp_stop();
                   syn_style(e->crate); syn_play(e->notes, e->count, echo_crates[e->crate].bpm, e->root, SEQ_LEAD | SEQ_FULL, 2);
                   show(IC_NOTE, e->title, echo_crates[e->crate].title); }
        } else if (btn(B_HOME)) { silence(); syn_earcon(EAR_BACK, 0); enter_main(1); }
        else if (syn_playing()) {
            int n = syn_note_index(); const echo_melody *e = &echo_melodies[lib_list[lib_i]];
            led_key(n >= 0 ? (int)e->notes[n * 2] - KEY_LOWEST_MIDI : -1);
        }
        break;
    case SCR_EXPLORER: explorer_tick(); break;
    case SCR_SETTINGS:
        if (d) { int n = (int)set_i + d; set_i = (unsigned)wrap(n, SETTINGS_ITEMS); tick_sound(set_i, SETTINGS_ITEMS, n != (int)set_i); announce_setting(0); }
        else if (btn(B_PLAY)) change_setting(1);
        else if (in->enc[1]) change_setting(in->enc[1] > 0 ? 1 : -1);
        else if (btn(B_HOME)) { syn_earcon(EAR_BACK, 0); enter_main(1); }
        break;
    case SCR_CONFIRM: {
        int side = key_side();
        if (btn(B_PLAY)) { say(echo_phrases[P_RESET_SURE]); break; }   /* repeat the question */
        if (side > 0) { uint8_t keep[12]; memcpy(keep, &sv.difficulty, 12); defaults(); memcpy(&sv.difficulty, keep, 12);
                           store(); say(echo_phrases[P_RESET_DONE]); screen = SCR_SETTINGS; show(IC_GEAR, "RESET PROGRESS", "DONE"); }
        else if (side < 0 || btn(B_HOME)) { screen = SCR_SETTINGS; announce_setting(0); }
        break;
    }
    case SCR_HELP:
        if (d) { int n = (int)help_i + d; help_i = (unsigned)wrap(n, HELP_PAGES); tick_sound(help_i, HELP_PAGES, n != (int)help_i); announce_help(); }
        else if (btn(B_PLAY)) { help_i = (help_i + 1) % HELP_PAGES; announce_help(); }
        else if (btn(B_HOME)) { syn_earcon(EAR_BACK, 0); enter_main(1); }
        break;
    }
    sp_pump();
}
