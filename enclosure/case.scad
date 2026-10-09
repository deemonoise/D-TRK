// SPDX-FileCopyrightText: 2026 deemonoise
// SPDX-License-Identifier: CERN-OHL-S-2.0
// Source location: https://github.com/deemonoise/D-TRK
// Корпус D-TRK (WT32-SC01 Plus). Все размеры в мм.
// Деталь: part = "top" | "bottom" | "clamp" | "assembly"
// Рендер: openscad -o case_top.stl -D 'part="top"' case.scad
// Вариант с 8 кнопками дорожек: -D 'trk=true'

part = "assembly";
trk  = false;     // сетка MX 4×4: Shift, Play, A, B, 8 кнопок дорожек, энкодер в блоке 2×2;
                  // PCF8575, ЦАП PCM5102A, усилитель MAX97220, гнездо наушников

$fn = 48;

// ---------- Замерить перед печатью ----------
// Модуль WT32-SC01 Plus (стекло + рамка + плата)
mod_w   = 92.0;   // по X (длинная сторона)
mod_d   = 60.0;   // по Y
mod_h   = 10.8;   // полная толщина
mod_clr = 0.2;    // зазор кармана на сторону

// Бортик над краем рамки
lip   = 1.0;      // ширина нахлёста на рамку
lip_t = 1.2;      // толщина бортика (глубина утопления модуля)

// Аккумулятор 103450
bat_w = 50; bat_d = 34; bat_h = 10;
bat_clr = 0.5;        // зазор в бортике
bat_gap = 1.5;        // от аккумулятора до низа модуля (скотч + запас)

// Модуль зарядки IP5306 (Type-C вход, USB-A выпаян)
ip_w = 20; ip_d = 26; ip_pcb = 1.2;  // ширина вдоль стенки, глубина внутрь
ip_rail = 1.5;        // высота подставки под платой
usbc_w = 9.2; usbc_h = 3.4;          // гнездо Type-C
usbc_plug_w = 12.5; usbc_plug_h = 7.0; usbc_plug_depth = 1.0; // раззенковка под штекер
ip_x = 25;            // центр по X

// Панельные компоненты задней стенки
jack_d   = 6.3;  jack_x = 55;   // PJ-392 MIDI OUT, резьба M6
phones_x = 41;                              // PJ-392 наушники (trk), между Type-C и MIDI
sw_d     = 6.3;  sw_x   = 73;   // MTS-102, резьба M6
back_z   = 9;                   // высота осей jack и тумблера над низом
label_h  = 2.8;  label_dp = 0.6;           // гравировка подписей над гнёздами (trk)

// Органы управления
mx_hole   = 14.0;   // вырез MX
mx_plate  = 1.5;    // толщина панели под защёлки MX
mx_pocket = 15.6;   // карман снизу вокруг выреза
ec11_d    = 7.2;    // отверстие EC11
sh_x = 22; pl_x = 44; enc_x = 80;   // центры по X (без кнопок дорожек)
ctrl_y = 17;                       // центр ряда по Y (без кнопок дорожек)
// trk: сетка 4×4 с шагом MX, ряды от экрана: Shift Play [энк]; A B [энк]; 1–4; 5–8.
// Энкодер — в центре блока 2×2 справа сверху (кноб ⌀30, до колпачков 4,5 мм).

// Сетка (trk = true)
trk_cols = 4; trk_rows = 4;
trk_pitch = 19;                    // шаг MX
trk_y0 = 17;                       // центр переднего ряда по Y (следующие — +trk_pitch)
// Модули на дне под кнопками (замерить): [ширина X, глубина Y, центр X, центр Y]
pcf_w = 32; pcf_d = 20;            // PCF8575
pcf_y = 26;
dac_w = 18; dac_d = 38;            // PCM5102 audio DAC v2 (чёрная), гребёнки по Y, штыри до ±25 мм от центра
dac_x = 22; dac_y = 62;
amp_w = 30; amp_d = 23;            // MAX97220 (по карточке)
amp_x = 71; amp_y = 66;

// Крепёж: вплавляемые гайки M3
ins_d     = 4.0;    // отверстие под гайку
ins_depth = 6.0;
post_r    = 3.5;
screw_d   = 3.4;    // проход M3
cb_d      = 6.2;    // цековка под голову M3 DIN 912 (⌀5,5 × 3)
cb_h      = 3.2;    // голова заподлицо; винты крышки M3 × 10

// Прижимные планки
clamp_w   = 8;
clamp_t   = 3;
foam_t    = 1.0;    // пористый скотч между планкой и модулем
clamp_dx  = 12;     // от края модуля по X до оси планки

// Корпус
wall   = 2.0;
top_t  = 2.5;
lid_t  = 3.0;       // дно нижней части
tray_h = 4.5;       // высота стенок корыта (аккумулятор ниже на столько же)
guide_t = 1.2;      // центрирующая губа по кромке корыта: толщина,
guide_h = 1.5;      //   высота,
guide_clr = 0.2;    //   зазор к внутренней стенке верха
corner_r = 3.0;
side_m = 1.0;       // зазор модуль — стенка по бокам
ctrl_d = trk ? 91 : 32;   // глубина переднего отсека (органы управления)
back_m = 9;         // полоса за модулем

// ---------- Производные ----------
pk_w = mod_w + 2*mod_clr;
pk_d = mod_d + 2*mod_clr;

W = pk_w + 2*side_m + 2*wall;
D = wall + ctrl_d + pk_d + back_m + wall;
H = lid_t + bat_h + bat_gap + mod_h + lip_t;

mod_x0 = wall + side_m;
mod_y0 = wall + ctrl_d;
mod_x1 = mod_x0 + pk_w;
mod_y1 = mod_y0 + pk_d;
mod_zb = H - lip_t - mod_h;           // низ модуля

clamp_zt = mod_zb - foam_t;           // верх планки = низ стоек планок
clamp_xs = [mod_x0 + clamp_dx, mod_x1 - clamp_dx];
clamp_ys = [mod_y0 - post_r - 1, mod_y1 + post_r + 1];

lid_posts = [[wall + post_r, wall + post_r], [W - wall - post_r, wall + post_r],
             [wall + post_r, D - wall - post_r], [W - wall - post_r, D - wall - post_r]];

ip_zc = lid_t + ip_rail + ip_pcb + usbc_h/2;   // ось Type-C

// Нижняя часть строится вниз от z = lid_t (стык с верхом), верх не зависит от tray_h
bot_z0 = lid_t - tray_h - lid_t;      // низ дна
floor_z = lid_t - tray_h;             // верх дна

echo(str("Корпус: ", W, " x ", D, " x ", H + tray_h, " мм"));
echo(str("Под MX до дна: ", H - mx_plate - floor_z, " мм"));

// центры MX: Shift, Play, (trk) A, B и 8 кнопок дорожек; кнопка 1 — левая в ряду ближе к экрану
function trk_cx(c) = W/2 + (c - (trk_cols - 1)/2) * trk_pitch;
function trk_cy(r) = trk_y0 + r * trk_pitch;   // r = 0 — передний ряд
row_y  = trk ? trk_cy(3) : ctrl_y;
shift_x = trk ? trk_cx(0) : sh_x;
play_x  = trk ? trk_cx(1) : pl_x;
enc_pos = trk ? [(trk_cx(2) + trk_cx(3))/2, (trk_cy(2) + trk_cy(3))/2] : [enc_x, ctrl_y];

mx_pos = concat([[shift_x, row_y], [play_x, row_y]],
    trk ? concat([[trk_cx(0), trk_cy(2)], [trk_cx(1), trk_cy(2)]],          // A, B
                 [for (r = [1, 0], c = [0 : trk_cols - 1]) [trk_cx(c), trk_cy(r)]]) : []);

// модули на дне: [w, d, x, y]
bay_mods = trk ? [[pcf_w, pcf_d, W/2, pcf_y], [dac_w, dac_d, dac_x, dac_y], [amp_w, amp_d, amp_x, amp_y]] : [];
for (m = bay_mods)
    assert(m[2] - m[0]/2 > wall + 1.5 && m[2] + m[0]/2 < W - wall - 1.5 &&
           m[3] - m[1]/2 > wall + 1.5 && m[3] + m[1]/2 < mod_y0 - 1, str("модуль вне отсека: ", m));

// ---------- Утилиты ----------
module rbox(w, d, h, r) {
    linear_extrude(h) offset(r) offset(-r) square([w, d]);
}

// ---------- Верх ----------
module top_shell() {
    difference() {
        union() {
            difference() {
                translate([0, 0, lid_t]) rbox(W, D, H - lid_t, corner_r);
                translate([wall, wall, lid_t - 0.01])
                    rbox(W - 2*wall, D - 2*wall, H - lid_t - top_t + 0.01, max(corner_r - wall, 0.5));
            }
            // угловые стойки под винты крышки
            for (p = lid_posts) translate([p[0], p[1], lid_t]) {
                cylinder(r = post_r, h = H - lid_t - top_t + 0.01);
                // стыковка со стенками угла
                translate([p[0] < W/2 ? -post_r - 0.5 : 0, p[1] < D/2 ? -post_r - 0.5 : 0, 0])
                    cube([post_r + 0.5, post_r + 0.5, H - lid_t - top_t + 0.01]);
            }
            // стойки прижимных планок
            for (x = clamp_xs, y = clamp_ys) translate([x, y, clamp_zt])
                cylinder(r = post_r, h = H - top_t - clamp_zt + 0.01);
        }
        // окно экрана и карман модуля
        translate([mod_x0 + lip, mod_y0 + lip, H - top_t - 1])
            cube([pk_w - 2*lip, pk_d - 2*lip, top_t + 2]);
        translate([mod_x0, mod_y0, mod_zb - 1])
            cube([pk_w, pk_d, H - lip_t - mod_zb + 1]);
        // MX
        for (p = mx_pos) {
            translate([p[0] - mx_hole/2, p[1] - mx_hole/2, H - top_t - 1])
                cube([mx_hole, mx_hole, top_t + 2]);
            translate([p[0] - mx_pocket/2, p[1] - mx_pocket/2, H - top_t - 1])
                cube([mx_pocket, mx_pocket, top_t - mx_plate + 1]);
        }
        // EC11
        translate([enc_pos[0], enc_pos[1], H - top_t - 1]) cylinder(d = ec11_d, h = top_t + 2);
        // гайки в стойках
        for (p = lid_posts) translate([p[0], p[1], lid_t - 0.01]) cylinder(d = ins_d, h = ins_depth);
        for (x = clamp_xs, y = clamp_ys) translate([x, y, clamp_zt - 0.01]) cylinder(d = ins_d, h = ins_depth);
        // задняя стенка: Type-C
        translate([ip_x, D - wall/2, ip_zc]) {
            cube([usbc_w, wall + 2, usbc_h], center = true);
            translate([0, wall/2 - usbc_plug_depth/2 + 0.01, 0])
                cube([usbc_plug_w, usbc_plug_depth + 0.02, usbc_plug_h], center = true);
        }
        // jack, наушники (trk) и тумблер
        for (h = concat([[jack_x, jack_d], [sw_x, sw_d]], trk ? [[phones_x, jack_d]] : []))
            translate([h[0], D + 1, back_z]) rotate([90, 0, 0]) cylinder(d = h[1], h = wall + 2);
        // подписи над гнёздами (читаются сзади)
        if (trk) for (l = [[jack_x, "MIDI"], [phones_x, "PHONES"]])
            translate([l[0], D - label_dp, back_z + jack_d/2 + 2.5]) rotate([90, 0, 180])
                linear_extrude(label_dp + 1)
                    text(l[1], size = label_h, font = "Liberation Sans:style=Bold", halign = "center");
    }
}

// ---------- Низ ----------
module fence(w, d, h, t = 1.2) {
    difference() {
        translate([-t, -t, 0]) cube([w + 2*t, d + 2*t, h]);
        translate([0, 0, -1]) cube([w, d, h + 2]);
    }
}

// губа: кольцо по внутренней стенке, без углов (там стойки верха) и без участка у IP5306;
// clr — отступ наружной грани от стенки (над стыком guide_clr, в корыте 0 — губа стоит на стенке и дне)
module guide(h, clr) {
    cr = max(corner_r - wall, 0.5);
    cw = wall + 2*post_r + 1;   // вырез угла
    ti = wall + guide_clr + guide_t;
    difference() {
        translate([wall + clr, wall + clr, 0])
            rbox(W - 2*wall - 2*clr, D - 2*wall - 2*clr, h, max(cr - clr, 0.3));
        translate([ti, ti, -1]) rbox(W - 2*ti, D - 2*ti, h + 2, 0.3);
        for (x = [0, W - cw], y = [0, D - cw]) translate([x, y, -1]) cube([cw, cw, h + 2]);
        translate([ip_x - ip_w/2 - 1.5, D - wall - 5, -1]) cube([ip_w + 3, 6, h + 2]);
    }
}

module bottom_lid() {
    difference() {
        union() {
            // корыто
            difference() {
                translate([0, 0, bot_z0]) rbox(W, D, lid_t + tray_h, corner_r);
                translate([wall, wall, floor_z])
                    rbox(W - 2*wall, D - 2*wall, tray_h + 1, max(corner_r - wall, 0.5));
            }
            // угловые стойки под винты крышки
            for (p = lid_posts) translate([p[0], p[1], floor_z - 0.01]) {
                cylinder(r = post_r, h = tray_h + 0.01);
                translate([p[0] < W/2 ? -post_r - 0.5 : 0, p[1] < D/2 ? -post_r - 0.5 : 0, 0])
                    cube([post_r + 0.5, post_r + 0.5, tray_h + 0.01]);
            }
            translate([0, 0, lid_t - 0.01]) guide(guide_h + 0.01, guide_clr);
            translate([0, 0, floor_z - 0.01]) guide(tray_h + 0.01, 0);
            // бортик аккумулятора, под модулем у передней кромки
            translate([(W - bat_w)/2 - bat_clr, mod_y0 + 2 - bat_clr, floor_z])
                fence(bat_w + 2*bat_clr, bat_d + 2*bat_clr, 2);
            // ложемент IP5306 у задней стенки; подставки выше на tray_h — Type-C напротив отверстия в верхе
            translate([ip_x - ip_w/2, D - wall - ip_d - 0.3, floor_z]) {
                // подставки под края платы
                for (dx = [0, ip_w - 1.5]) translate([dx, 0, 0]) cube([1.5, ip_d, ip_rail + tray_h]);
                // боковые упоры и задний упор
                difference() {
                    translate([-0.3, 0, 0]) fence(ip_w + 0.6, ip_d + 0.3, ip_rail + tray_h + ip_pcb + 0.6);
                    translate([-2, ip_d - 1, -1]) cube([ip_w + 4, 5, 20]);   // открыто к стенке
                }
            }
            // бортики PCF8575, ЦАП и усилителя
            for (m = bay_mods) translate([m[2] - m[0]/2 - 0.3, m[3] - m[1]/2 - 0.3, floor_z])
                fence(m[0] + 0.6, m[1] + 0.6, 1.5);
        }
        // винты M3 × 10, голова утоплена
        for (p = lid_posts) translate([p[0], p[1], bot_z0 - 1]) {
            cylinder(d = screw_d, h = lid_t + tray_h + 2);
            cylinder(d = cb_d, h = cb_h + 1);
        }
    }
}

// ---------- Прижимная планка ----------
clamp_len = clamp_ys[1] - clamp_ys[0] + 2*post_r + 1;

module clamp() {
    difference() {
        translate([-clamp_w/2, -post_r - 0.5, 0]) cube([clamp_w, clamp_len, clamp_t]);
        for (y = [0, clamp_ys[1] - clamp_ys[0]])
            translate([0, y, -1]) cylinder(d = screw_d, h = clamp_t + 2);
    }
}

// ---------- Сборка ----------
module assembly() {
    color("gray", 0.6) top_shell();
    color("dimgray") bottom_lid();
    for (x = clamp_xs) color("orange") translate([x, clamp_ys[0], clamp_zt - clamp_t]) clamp();
    // макеты
    color("black") translate([mod_x0 + mod_clr, mod_y0 + mod_clr, mod_zb]) cube([mod_w, mod_d, mod_h]);
    color("silver") translate([(W - bat_w)/2, mod_y0 + 2, floor_z]) cube([bat_w, bat_d, bat_h]);
    color("blue") translate([ip_x - ip_w/2, D - wall - ip_d, lid_t + ip_rail]) cube([ip_w, ip_d, ip_pcb]);
    for (m = bay_mods) color("green") translate([m[2] - m[0]/2, m[3] - m[1]/2, floor_z]) cube([m[0], m[1], 1.6]);
    // MX под панелью (корпус 14×14, 5 мм вниз + выводы), колпачки 18×18
    for (p = mx_pos) {
        color("white") translate([p[0] - 7, p[1] - 7, H - mx_plate - 8.3]) cube([14, 14, 8.3]);
        color("lightgray") translate([p[0] - 9, p[1] - 9, H + 5]) cube([18, 18, 8]);
    }
    // кноб ⌀30
    color("darkgray") translate([enc_pos[0], enc_pos[1], H + 1]) cylinder(d = 30, h = 17);
}

if (part == "top")         translate([0, D, H]) rotate([180, 0, 0]) top_shell();   // лицом на стол
else if (part == "bottom") translate([0, 0, -bot_z0]) bottom_lid();
else if (part == "clamp")  clamp();
else                       assembly();
