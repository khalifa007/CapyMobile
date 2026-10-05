// Original 5x7 bitmap glyphs for printable ASCII, space to tilde. No font files required.
static const unsigned char glyphs[95][7] = {
 {0,0,0,0,0,0,0},{4,4,4,4,0,0,4},{10,10,10,0,0,0,0},{10,10,31,10,31,10,10},
 {4,15,20,14,5,30,4},{24,25,2,4,8,19,3},{12,18,20,8,21,18,13},{12,4,8,0,0,0,0},
 {2,4,8,8,8,4,2},{8,4,2,2,2,4,8},{0,4,21,14,21,4,0},{0,4,4,31,4,4,0},
 {0,0,0,0,12,4,8},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12},{0,1,2,4,8,16,0},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},{0,12,12,0,12,12,0},{0,12,12,0,12,4,8},
 {2,4,8,16,8,4,2},{0,0,31,0,31,0,0},{8,4,2,1,2,4,8},{14,17,1,2,4,0,4},
 {14,17,1,13,21,21,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,14},
 {17,17,17,31,17,17,17},{31,4,4,4,4,4,31},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},
 {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},
 {31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,27,17},
 {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},{14,8,8,8,8,8,14},
 {0,16,8,4,2,1,0},{14,2,2,2,2,2,14},{4,10,17,0,0,0,0},{0,0,0,0,0,0,31},
 {8,4,2,0,0,0,0},{0,0,14,1,15,17,15},{16,16,22,25,17,17,30},{0,0,14,16,16,17,14},
 {1,1,13,19,17,17,15},{0,0,14,17,31,16,14},{6,9,8,28,8,8,8},{0,15,17,17,15,1,14},
 {16,16,22,25,17,17,17},{4,0,12,4,4,4,14},{2,0,6,2,2,18,12},{16,16,18,20,24,20,18},
 {12,4,4,4,4,4,14},{0,0,26,21,21,17,17},{0,0,22,25,17,17,17},{0,0,14,17,17,17,14},
 {0,0,30,17,30,16,16},{0,0,13,19,15,1,1},{0,0,22,25,16,16,16},{0,0,14,16,14,1,30},
 {8,8,28,8,8,9,6},{0,0,17,17,17,19,13},{0,0,17,17,17,10,4},{0,0,17,17,21,21,10},
 {0,0,17,10,4,10,17},{0,0,17,17,15,1,14},{0,0,31,2,4,8,31},{2,4,4,8,4,4,2},
 {4,4,4,4,4,4,4},{8,4,4,2,4,4,8},{0,0,8,21,2,0,0}
};

static inline void rect(uint32_t *pixels, int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y+h && row < HEIGHT; ++row)
        for (int col = x; col < x+w && col < WIDTH; ++col)
            if (row >= 0 && col >= 0) pixels[row*WIDTH+col] = 0xff000000u | color;
}

// Characters outside printable ASCII leave a blank cell.
static inline void text(uint32_t *pixels, int x, int y, int scale, const char *s, uint32_t color) {
    for (; *s; ++s, x += 6*scale) {
        unsigned char ch = (unsigned char)*s;
        if (ch <= ' ' || ch > '~') continue;
        for (int row=0; row<7; ++row)
            for (int col=0; col<5; ++col)
                if (glyphs[ch-' '][row] & (1 << (4-col)))
                    rect(pixels, x+col*scale, y+row*scale, scale, scale, color);
    }
}

// ---------- controller buttons ----------
// ui_text() draws text in which {X} {O} {SQ} {TRI} {DPAD} {L1} {R1} {L2} {R2} {OPT} become button icons.

static inline void disc(uint32_t *px, int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; ++dy) {
        int w = 0;
        while ((w + 1) * (w + 1) + dy * dy <= r * r) ++w;
        rect(px, cx - w, cy + dy, 2 * w + 1, 1, color);
    }
}

static inline void ring(uint32_t *px, int cx, int cy, int r, int thick, uint32_t color) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            int d = dx * dx + dy * dy;
            if (d <= r * r && d >= (r - thick) * (r - thick)) rect(px, cx + dx, cy + dy, 1, 1, color);
        }
}

static inline void stroke(uint32_t *px, int x0, int y0, int x1, int y1, int thick, uint32_t color) {
    int dx = x1 - x0, dy = y1 - y0, steps = dx < 0 ? -dx : dx;
    if ((dy < 0 ? -dy : dy) > steps) steps = dy < 0 ? -dy : dy;
    for (int i = 0; i <= steps; ++i) {
        int x = steps ? x0 + dx * i / steps : x0, y = steps ? y0 + dy * i / steps : y0;
        rect(px, x - thick / 2, y - thick / 2, thick, thick, color);
    }
}

static const struct { const char *token, *label; uint32_t color; } buttons_drawn[] = {
    {"{X}", NULL, 0x8fb8ff}, {"{O}", NULL, 0xff7a85}, {"{SQ}", NULL, 0xf3a0dc}, {"{TRI}", NULL, 0x4ee0b0},
    {"{DPAD}", NULL, 0}, {"{L1}", "L1", 0}, {"{R1}", "R1", 0}, {"{L2}", "L2", 0}, {"{R2}", "R2", 0}, {"{OPT}", "OPTIONS", 0},
};

// Which button a token at s names, or -1.
static inline int button_at(const char *s) {
    if (*s != '{') return -1;
    for (int i = 0; i < (int)(sizeof(buttons_drawn) / sizeof(buttons_drawn[0])); ++i)
        if (!strncmp(s, buttons_drawn[i].token, strlen(buttons_drawn[i].token))) return i;
    return -1;
}

static inline int button_width(int b, int scale) {
    int h = 10 * scale;
    if (!buttons_drawn[b].label) return h;
    int small = scale > 1 ? scale - 1 : 1;
    return (int)strlen(buttons_drawn[b].label) * 6 * small + h / 2;
}

// Draws one button icon whose left edge is x, centred on text of this scale drawn at y.
static inline void button(uint32_t *px, int x, int y, int scale, int b) {
    int h = 10 * scale, top = y + 7 * scale / 2 - h / 2, cx = x + h / 2, cy = top + h / 2, t = scale;
    uint32_t color = buttons_drawn[b].color;
    if (buttons_drawn[b].label) {
        // Shoulder and OPTIONS buttons: a light rounded plate with the name on it.
        int w = button_width(b, scale), r = h / 2, small = scale > 1 ? scale - 1 : 1;
        disc(px, x + r, cy, r, 0xc4d0dc);
        disc(px, x + w - r - 1, cy, r, 0xc4d0dc);
        rect(px, x + r, top, w - 2 * r, h + 1, 0xc4d0dc);
        text(px, x + h / 4, cy - 7 * small / 2, small, buttons_drawn[b].label, 0x101923);
        return;
    }
    if (!strcmp(buttons_drawn[b].token, "{DPAD}")) {
        int arm = h / 3;
        rect(px, cx - arm / 2, top, arm, h, 0xc4d0dc);
        rect(px, x, cy - arm / 2, h, arm, 0xc4d0dc);
        rect(px, cx - arm / 4, cy - arm / 4, arm / 2, arm / 2, 0x101923);
        return;
    }
    // Face buttons: the coloured symbol on a dark disc, as on the controller.
    disc(px, cx, cy, h / 2, 0x2c3e50);
    int k = h * 22 / 100;
    switch (buttons_drawn[b].token[1]) {
    case 'X': stroke(px, cx - k, cy - k, cx + k, cy + k, t, color); stroke(px, cx - k, cy + k, cx + k, cy - k, t, color); break;
    case 'O': ring(px, cx, cy, h * 27 / 100, t, color); break;
    case 'S': rect(px, cx - k, cy - k, 2 * k + 1, t, color); rect(px, cx - k, cy + k - t + 1, 2 * k + 1, t, color);
              rect(px, cx - k, cy - k, t, 2 * k + 1, color); rect(px, cx + k - t + 1, cy - k, t, 2 * k + 1, color); break;
    default: {
        int ty = cy - h * 26 / 100, by = cy + h * 18 / 100, half = h * 28 / 100;
        stroke(px, cx, ty, cx - half, by, t, color); stroke(px, cx, ty, cx + half, by, t, color); stroke(px, cx - half, by, cx + half, by, t, color);
    }
    }
}

static inline int ui_width(const char *s, int scale) {
    int w = 0;
    while (*s) {
        int b = button_at(s);
        if (b >= 0) { w += button_width(b, scale) + scale * 2; s += strlen(buttons_drawn[b].token); }
        else { w += 6 * scale; ++s; }
    }
    return w;
}

static inline void ui_text(uint32_t *px, int x, int y, int scale, const char *s, uint32_t color) {
    while (*s) {
        int b = button_at(s);
        if (b >= 0) {
            button(px, x, y, scale, b);
            x += button_width(b, scale) + scale * 2;
            s += strlen(buttons_drawn[b].token);
        } else {
            char one[2] = {*s++, 0};
            text(px, x, y, scale, one, color);
            x += 6 * scale;
        }
    }
}
