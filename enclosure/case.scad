// SPDX-FileCopyrightText: 2026 deemonoise
// SPDX-License-Identifier: CERN-OHL-S-2.0
// Source location: https://github.com/deemonoise/D-TRK
// Корпус D-TRK (WT32-SC01 Plus). Все размеры в мм.
// Деталь: part = "top" | "bottom" | "clamp" | "clamp2" | "assembly"
// Рендер: openscad -o case_top.stl -D 'part="top"' case.scad
// Вариант с 8 кнопками дорожек: -D 'trk=true'

part = "assembly";
trk  = false;     // сетка MX 4×4: Shift, Play, A, B, 8 кнопок дорожек, энкодер в блоке 2×2;
                  // PCF8575, синт-плата Teensy 4.1 и Audio Shield Rev D на пазах, гнездо наушников спереди

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
jack_d   = 6.3;  jack_x = trk ? 43.5 : 55;   // PJ-392 MIDI OUT, резьба M6 (trk: правее — MIC)
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
pcf_x = 71; pcf_y = 66;            // за шилдом
// Пазы под платы (Teensy, шилд, IP5306): полка под краем, стенка, губа над краем
rail_w = 1.4;                      // от края платы до наружной грани паза
rail_clr = 0.15;                   // зазор края платы к стенке паза
rail_lip = 0.8;                    // полка и губа заходят на плату
rail_lip_t = 1.2;                  // толщина губы
rail_gap = 0.15;                   // зазор над платой под губой
snap_h = 0.25; snap_l = 2;         // защёлка: клин на полке за краем платы (проход с натягом 0,1)
// Синт-плата Teensy 4.1 (trk): вдоль Y, слот microSD — к передней стенке, USB — назад.
// Заезжает сзади вперёд: пазы с губами — на передней части (там нет проводов),
// задняя часть (провода) лежит на двух рейках между рядами пинов. Назад держит упор верхней части за USB.
teensy_w = 18; teensy_d = 61;      // плата
teensy_pcb = 1.6;
teensy_x = 21;                     // центр по X
teensy_ledge = 3;                  // высота полок и реек: место под хвосты проводов снизу
teensy_lip_len = 30;               // пазы от переднего края (ближний провод — пин 8, ≈35 мм)
teensy_rail_dx = 4.5;              // оси задних реек от центра платы (ряды пинов — ±7,6)
teensy_rail_len = 26;              // задние рейки от края с USB
teensy_usb_out = 0.8;              // USB выступает за край платы (замерить)
teensy_stop_w = 8; teensy_stop_t = 1.8;   // упор за USB на верхней части
teensy_sd_out = 2.5;               // карта выступает за край платы (замерить)
teensy_sd_z = 1.0;                 // ось карты над верхом платы (замерить)
teensy_sd_slot_w = 13; teensy_sd_slot_h = 3;   // прорезь под карту (карта 11 × 0,8)
teensy_sd_notch_d = 12;            // выемка под палец снаружи
teensy_sd_skin = 0.8;              // стенка, остающаяся за выемкой
// Audio Shield Rev D (trk): справа от Teensy, на проводах; гнездо наушников — в переднюю стенку.
// Плата по футпринту PJRC 35,56 × 36,83; гнездо на крае 36,83, ближе к правому углу (вид сверху, гнездо к себе).
// Заезжает сзади вперёд по пазам на всю длину, назад держит защёлка.
aud_w = 36.83; aud_d = 35.56;      // по X; по Y (передний край — с гнездом)
aud_pcb = 1.6;
aud_x0 = 34;                       // левый край платы
aud_ledge = 2;                     // высота полок: хвосты пайки снизу
aud_jack_dx = 31.7;                // ось гнезда от левого края платы (по футпринту, сверить)
aud_jack_z = 2.5;                  // ось гнезда над верхом платы (замерить)
aud_jack_out = 0.5;                // гнездо выступает за край платы (замерить)
aud_jack_hole = 6.5;               // отверстие под штекер 3,5 мм
// Микрофон и линейный вход (trk): стоят на язычках нижней части — язычок-кусок стенки
// поднимается от стыка и заходит в вырез стенки верха, гребни по бокам — в пазы на внутренней половине стенки.
// MIC — задняя стенка между Type-C и MIDI, LINE IN (PJ-392, M6) — передняя справа от PHONES.
mic_x = 57; mic_z = 9;             // центр капсюля: x, высота оси (между MIDI и тумблером)
mic_cup_d = 11.2;                  // стакан: капсюль 9767 ⌀9,7 + пористый скотч 0,75 на сторону
mic_cup_t = 1.0; mic_cup_h = 7.5;  // стенка и глубина стакана (капсюль 6,7 + ткань)
mic_hole_d = 1.2; mic_grille_r = 2.6;   // решётка: центр + 6 отверстий по окружности
lin_x = 81;                        // LINE IN: центр x, ось — на высоте PHONES
jack_nut = 10;                     // гайка M6 PJ-392 и MTS-102 (по углам, с запасом)
jack_body = 8; sw_body = 13.2;     // корпус PJ-392 (квадрат); MTS-102 вдоль стенки
tab_f_top = 10.5; tab_b_top = 13.5; // верх язычков (z): передний (PHONES, LINE), задний (MIC, тумблер)
tab_g = 1.0;                       // гребни: заходят в пазы на столько с каждой стороны
tab_clr = 0.2;                     // зазор язычка в вырезе
lin_rib_l = 4; lin_rib_t = 1.2;    // рёбра по бокам корпуса LINE

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
// Бобышки на задней стороне модуля (клэмпы v2)
boss_x0 = 10.34;   // центр от левого края модуля, смотря на экран (правый, смотря на плату)
boss_x1 = 6.52;    // центр от правого края, смотря на экран
boss_y  = 4.3;     // центр от длинных краёв
boss_d  = 6.0;     // наружный диаметр (не замерен)
boss_hole = 2.5;   // отверстие (не замерено)
boss_flip = false; // true — модуль развёрнут на 180° в плоскости экрана
pin_d = 2.0; pin_h = 1.5;   // штырёк лапки в отверстие бобышки

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
boss_xs = [mod_x0 + mod_clr + (boss_flip ? boss_x1 : boss_x0),
           mod_x1 - mod_clr - (boss_flip ? boss_x0 : boss_x1)];
boss_ys = [mod_y0 + mod_clr + boss_y, mod_y1 - mod_clr - boss_y];

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
bay_mods = trk ? [[pcf_w, pcf_d, pcf_x, pcf_y]] : [];
for (m = bay_mods)
    assert(m[2] - m[0]/2 > wall + 1.5 && m[2] + m[0]/2 < W - wall - 1.5 &&
           m[3] - m[1]/2 > wall + 1.5 && m[3] + m[1]/2 < mod_y0 - 1, str("модуль вне отсека: ", m));

// Teensy: передний край платы у стенки (губа там вырезана), карта уходит в прорезь
teensy_x0 = teensy_x - teensy_w/2;
teensy_y0 = wall + 0.5;
teensy_y = teensy_y0 + teensy_d/2;
teensy_zt = floor_z + teensy_ledge + teensy_pcb;                 // верх платы
teensy_zc = teensy_zt + teensy_sd_z;                             // ось карты
// шилд: гнездо у передней стенки
aud_y0 = wall + 0.2 + aud_jack_out;
aud_zb = floor_z + aud_ledge;                                    // низ платы
aud_jx = aud_x0 + aud_jack_dx;
aud_jz = aud_zb + aud_pcb + aud_jack_z;                          // ось гнезда
// IP5306: задний край у стенки
ip_y0 = D - wall - ip_d - 0.3;

// платы на пазах (trk) с пазами: [x0, y0, x1, y1]
bay_boards = trk ? [[teensy_x0 - rail_w, teensy_y0, teensy_x0 + teensy_w + rail_w, teensy_y0 + teensy_d + teensy_usb_out],
                    [aud_x0 - rail_w, aud_y0, aud_x0 + aud_w + rail_w, aud_y0 + aud_d + 0.2 + snap_l]] : [];
for (b = bay_boards) {
    assert(b[0] > wall + guide_clr + guide_t && b[2] < W - wall - guide_clr - guide_t && b[3] < mod_y0 - 1,
           str("плата вне отсека: ", b));
    assert(b[0] > lid_posts[0][0] + post_r + 0.5 || b[1] > lid_posts[0][1] + post_r + 0.5, "плата на стойке");
    for (m = bay_mods)
        assert(m[2] + m[0]/2 + 1 < b[0] || m[2] - m[0]/2 - 1 > b[2] ||
               m[3] + m[1]/2 + 1 < b[1] || m[3] - m[1]/2 - 1 > b[3], str("модуль на месте платы: ", m));
}
if (trk) assert(bay_boards[0][2] + 0.5 < bay_boards[1][0], "шилд на месте Teensy");
echo(str("Гнездо наушников: x = ", aud_jx, ", ось z = ", aud_jz, "; карта: x = ", teensy_x, ", ось z = ", teensy_zc));

// Язычки нижней части (trk): передний — PHONES и LINE IN, задний — MIC и тумблер; [x0, x1, верх]
mic_od = mic_cup_d + 2*mic_cup_t;
lin_z = aud_jz;
tab_f = [aud_jx - aud_jack_hole/2 - 2, lin_x + jack_nut/2 + 1, tab_f_top];
tab_b = [mic_x - mic_od/2, sw_x + jack_nut/2 + 1, tab_b_top];
lab_z = back_z + jack_d/2 + 2.5;                                 // база подписей сзади
flab_z = tab_f_top + tab_clr + 1.3;                              // база подписей спереди
if (trk) {
    // корпус MIDI над рейками IP5306 (их верх — низ губы + толщина губы)
    ip_rail_x1 = ip_x + ip_w/2 + rail_w;
    ip_rail_zt = floor_z + ip_rail + tray_h + ip_pcb + rail_gap + rail_lip_t;
    assert(jack_x - jack_body/2 > ip_rail_x1 + 0.5 || back_z - jack_body/2 > ip_rail_zt, "MIDI на рейках IP5306");
    assert(tab_b[0] - tab_g > jack_x + jack_d/2 + 1, "задний язычок на отверстии MIDI");
    assert(mic_x - mic_od/2 > jack_x + jack_body/2 + 0.5, "стакан MIC на корпусе MIDI");
    assert(mic_x + mic_od/2 < sw_x - sw_body/2 - 0.5, "стакан MIC на корпусе тумблера");
    assert(lin_x - jack_nut/2 > aud_jx + aud_jack_hole/2 + 1, "LINE на месте PHONES");
    assert(lin_x - jack_body/2 > aud_x0 + aud_w + rail_w + 0.5, "LINE на пазах шилда");
    assert(lin_z - jack_nut/2 > bot_z0 && lin_z - jack_body/2 > floor_z + 0.5, "LINE ниже дна");
    for (t = [tab_f, tab_b])
        assert(t[1] + tab_g - tab_clr < lid_posts[1][0] - post_r - 0.3, "язычок на угловой стойке");
    // отверстия целиком в язычке (гайки снаружи могут заходить на верх — прижимают и его)
    assert(tab_f_top > lin_z + jack_d/2 + 1 && tab_b_top > max(mic_z + mic_grille_r + mic_hole_d, back_z + sw_d/2 + 1),
           "отверстие выше язычка");
    assert(tab_b_top + tab_clr < lab_z - 0.5, "подпись на вырезе");
    echo(str("MIDI: x = ", jack_x, "; MIC: x = ", mic_x, "; тумблер: x = ", sw_x, "; LINE: x = ", lin_x, ", ось z = ", lin_z));
    echo(str("Язычки: передний x ", tab_f[0], "…", tab_f[1], ", задний x ", tab_b[0], "…", tab_b[1]));
}

// ---------- Утилиты ----------
module rbox(w, d, h, r) {
    linear_extrude(h) offset(r) offset(-r) square([w, d]);
}

// Прорезь к слоту microSD Teensy в передней стенке (режет и верх, и низ) и выемка под палец
module teensy_sd_cut() {
    translate([teensy_x - teensy_sd_slot_w/2, -1, teensy_zc - teensy_sd_slot_h/2])
        cube([teensy_sd_slot_w, 1 + wall + guide_clr + guide_t + 1, teensy_sd_slot_h]);
    translate([teensy_x, -1, teensy_zc]) rotate([-90, 0, 0])
        cylinder(d = teensy_sd_notch_d, h = 1 + wall - teensy_sd_skin);
}

// Отверстие под штекер наушников в передней стенке (режет и верх, и низ)
module aud_jack_cut() {
    translate([aud_jx, -1, aud_jz]) rotate([-90, 0, 0]) cylinder(d = aud_jack_hole, h = 1 + wall + 0.1);
}

// Локальная система стенки: y = 0 — наружная грань, y растёт внутрь; side 0 — передняя, 1 — задняя
module at_wall(side) {
    if (side == 0) children();
    else translate([0, D, 0]) mirror([0, 1, 0]) children();
}

// Вырез в стенке верха под язычок t = [x0, x1, верх] и пазы под гребни на внутренней половине стенки
module tab_cut(t) {
    translate([t[0], -1, lid_t - 1]) cube([t[1] - t[0], wall + 1.01, t[2] + tab_clr - lid_t + 1]);
    translate([t[0] - tab_g, wall/2, lid_t - 1])
        cube([t[1] - t[0] + 2*tab_g, wall/2 + 0.01, t[2] + tab_clr - lid_t + 1]);
}

// Язычок нижней части: в вырезе — уже на зазор, гребни — на внутренней половине стенки
module tab_body(t) {
    translate([t[0] + tab_clr, 0, lid_t - 0.01]) cube([t[1] - t[0] - 2*tab_clr, wall, t[2] - lid_t + 0.01]);
    translate([t[0] - tab_g + tab_clr, wall/2 + tab_clr, lid_t - 0.01])
        cube([t[1] - t[0] + 2*tab_g - 2*tab_clr, wall/2 - tab_clr, t[2] - lid_t + 0.01]);
}

// Задний язычок (локально): MIC в стакане на подставке до дна, тумблер на гайке
module back_tab() {
    tab_body(tab_b);
    translate([mic_x, wall - 0.01, mic_z]) rotate([-90, 0, 0]) cylinder(d = mic_od, h = mic_cup_h + 0.01);
    translate([mic_x - mic_od/2, wall - 0.01, floor_z - 0.01]) cube([mic_od, mic_cup_h + 0.01, mic_z - floor_z]);
}
module back_tab_cut() {
    translate([mic_x, wall, mic_z]) rotate([-90, 0, 0]) cylinder(d = mic_cup_d, h = mic_cup_h + 1);
    for (p = concat([[0, 0]], [for (a = [0 : 60 : 300]) mic_grille_r * [cos(a), sin(a)]]))
        translate([mic_x + p[0], -1, mic_z + p[1]]) rotate([-90, 0, 0]) cylinder(d = mic_hole_d, h = wall + 2, $fn = 12);
    translate([sw_x, -1, back_z]) rotate([-90, 0, 0]) cylinder(d = sw_d, h = wall + 2);
}

// Передний язычок: PHONES (отверстие — aud_jack_cut), LINE IN на гайке, рёбра до дна по бокам корпуса LINE
module front_tab() {
    tab_body(tab_f);
    for (s = [-1, 1]) translate([lin_x + s * (jack_body/2 + 0.5 + lin_rib_t/2) - lin_rib_t/2, wall - 0.01, 0])
        rotate([90, 0, 90]) linear_extrude(lin_rib_t)
            polygon([[0, floor_z], [lin_rib_l, floor_z], [lin_rib_l, lid_t], [0, tab_f_top]]);
}
module front_tab_cut() {
    translate([lin_x, -1, lin_z]) rotate([-90, 0, 0]) cylinder(d = jack_d, h = wall + 2);
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
            // упор за USB Teensy: не даёт плате уехать назад из пазов
            if (trk) translate([teensy_x - teensy_stop_w/2, teensy_y0 + teensy_d + teensy_usb_out + 0.3, teensy_zt + 0.5])
                cube([teensy_stop_w, teensy_stop_t, H - top_t - teensy_zt - 0.5 + 0.01]);
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
        if (trk) {
            teensy_sd_cut();
            aud_jack_cut();
            // подпись над гнездом наушников (читается спереди)
            translate([aud_jx, label_dp, flab_z]) rotate([90, 0, 0])
                linear_extrude(label_dp + 1)
                    text("PHONES", size = label_h, font = "Liberation Sans:style=Bold", halign = "center");
            // вырезы под язычки, подписи над ними
            at_wall(1) tab_cut(tab_b);
            at_wall(0) tab_cut(tab_f);
            translate([lin_x, label_dp, flab_z]) rotate([90, 0, 0]) linear_extrude(label_dp + 1)
                text("LINE", size = label_h, font = "Liberation Sans:style=Bold", halign = "center");
            translate([mic_x, D - label_dp, lab_z]) rotate([90, 0, 180]) linear_extrude(label_dp + 1)
                text("MIC", size = label_h, font = "Liberation Sans:style=Bold", halign = "center");
        }
        // jack и тумблер (trk: тумблер на заднем язычке)
        for (h = trk ? [[jack_x, jack_d]] : [[jack_x, jack_d], [sw_x, sw_d]])
            translate([h[0], D + 1, back_z]) rotate([90, 0, 0]) cylinder(d = h[1], h = wall + 2);
        // подпись над MIDI (читается сзади)
        if (trk)
            translate([jack_x, D - label_dp, back_z + jack_d/2 + 2.5]) rotate([90, 0, 180])
                linear_extrude(label_dp + 1)
                    text("MIDI", size = label_h, font = "Liberation Sans:style=Bold", halign = "center");
    }
}

// ---------- Низ ----------
module fence(w, d, h, t = 1.2) {
    difference() {
        translate([-t, -t, 0]) cube([w + 2*t, d + 2*t, h]);
        translate([0, 0, -1]) cube([w, d, h + 2]);
    }
}

// Пазы по боковым краям платы: плата заезжает вдоль Y и сидит под губами.
// Локально: x = 0 — левый край платы, y = 0 — передний край, z = 0 — дно. w × len — плата,
// z0 — высота полки (низ платы), ys — участок пазов по Y, lips — участок с губами,
// stop — упор у переднего края (от ys[0] до 0), snap: 1 — защёлка за задним краем, −1 — за передним
module slide(w, len, z0, pcb, ys, lips, stop = false, snap = 0) {
    zl = z0 + pcb + rail_gap;                       // низ губы
    for (s = [0, 1]) translate([s * w, 0, 0]) mirror([s, 0, 0]) {
        translate([-rail_w, ys[0], 0]) {
            cube([rail_w + rail_lip, ys[1] - ys[0], z0]);                // полка
            cube([rail_w - rail_clr, ys[1] - ys[0], zl + rail_lip_t]);   // стенка
        }
        translate([-rail_w, lips[0], zl]) cube([rail_w + rail_lip, lips[1] - lips[0], rail_lip_t]);
        if (stop) translate([-rail_w, ys[0], 0]) cube([rail_w + rail_lip, -ys[0], zl + rail_lip_t]);
        // клин: пологий к входу, отвесный к плате; назад плату не пускает губа
        if (snap != 0) translate([-rail_w, 0, z0 - 0.01]) rotate([90, 0, 90]) linear_extrude(rail_w + rail_lip)
            polygon(snap > 0 ? [[len + 0.2, 0], [len + 0.2, snap_h + 0.01], [len + 0.2 + snap_l, 0]]
                             : [[-0.2, 0], [-0.2, snap_h + 0.01], [-0.2 - snap_l, 0]]);
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
            // ложемент IP5306 у задней стенки (без кнопок дорожек); подставки выше на tray_h — Type-C напротив отверстия в верхе
            if (!trk) translate([ip_x - ip_w/2, ip_y0, floor_z]) {
                // подставки под края платы
                for (dx = [0, ip_w - 1.5]) translate([dx, 0, 0]) cube([1.5, ip_d, ip_rail + tray_h]);
                // боковые упоры и задний упор
                difference() {
                    translate([-0.3, 0, 0]) fence(ip_w + 0.6, ip_d + 0.3, ip_rail + tray_h + ip_pcb + 0.6);
                    translate([-2, ip_d - 1, -1]) cube([ip_w + 4, 5, 20]);   // открыто к стенке
                }
            }
            // бортики PCF8575
            for (m = bay_mods) translate([m[2] - m[0]/2 - 0.3, m[3] - m[1]/2 - 0.3, floor_z])
                fence(m[0] + 0.6, m[1] + 0.6, 1.5);
            // язычки: сзади MIC и тумблер, спереди PHONES и LINE IN
            if (trk) {
                at_wall(1) back_tab();
                at_wall(0) front_tab();
            }
        }
        if (trk) {
            teensy_sd_cut();
            aud_jack_cut();
            at_wall(1) back_tab_cut();
            at_wall(0) front_tab_cut();
            // губы у передней стенки напротив Teensy, шилда и корпуса LINE: подходят к стенке вплотную
            for (x = [[teensy_x0, teensy_w], [aud_x0, aud_w]])
                translate([x[0] - rail_w - 0.5, wall - 0.01, floor_z + 0.01])
                    cube([x[1] + 2*rail_w + 1, guide_clr + guide_t + 0.5, lid_t + guide_h - floor_z + 1]);
            translate([lin_x - jack_body/2 - 0.3, wall - 0.01, floor_z + 0.01])
                cube([jack_body + 0.6, guide_clr + guide_t + 0.5, lid_t + guide_h - floor_z + 1]);
        }
        // винты M3 × 10, голова утоплена
        for (p = lid_posts) translate([p[0], p[1], bot_z0 - 1]) {
            cylinder(d = screw_d, h = lid_t + tray_h + 2);
            cylinder(d = cb_d, h = cb_h + 1);
        }
    }
    if (trk) translate([0, 0, floor_z - 0.01]) {
        // Teensy: пазы спереди (сзади вперёд до упора у стенки), рейки под частью с проводами
        translate([teensy_x0, teensy_y0, 0]) {
            slide(teensy_w, teensy_d, teensy_ledge + 0.01, teensy_pcb,
                  [wall - teensy_y0 - 0.01, teensy_lip_len], [wall - teensy_y0 - 0.01, teensy_lip_len], stop = true);
            for (dx = [-teensy_rail_dx, teensy_rail_dx])
                translate([teensy_w/2 + dx - 0.75, teensy_d - teensy_rail_len, 0]) cube([1.5, teensy_rail_len, teensy_ledge + 0.01]);
        }
        // шилд: пазы на всю длину, защёлка за задним краем
        translate([aud_x0, aud_y0, 0])
            slide(aud_w, aud_d, aud_ledge + 0.01, aud_pcb, [wall - aud_y0 - 0.01, aud_d + 0.2 + snap_l],
                  [wall - aud_y0 - 0.01, aud_d], stop = true, snap = 1);
        // IP5306: заезжает назад до стенки, защёлка перед передним краем
        translate([ip_x - ip_w/2, ip_y0, 0])
            slide(ip_w, ip_d, ip_rail + tray_h + 0.01, ip_pcb, [-0.2 - snap_l, ip_d + 0.31], [0, ip_d + 0.31], snap = -1);
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

// ---------- Лапка v2: винт на стойке, площадка со штырьком на бобышке ----------
// (dx, dy) — смещение центра бобышки от винта
module clamp2(dx, dy) {
    difference() {
        union() {
            hull() {
                cylinder(r = post_r, h = clamp_t);
                translate([dx, dy, 0]) cylinder(d = boss_d, h = clamp_t);
            }
            translate([dx, dy, clamp_t - 0.01]) {
                cylinder(d = boss_d, h = foam_t + 0.01);
                cylinder(d = pin_d, h = foam_t + pin_h + 0.01);
            }
        }
        translate([0, 0, -1]) cylinder(d = screw_d, h = clamp_t + 2);
    }
}
function clamp2_off(i, j) = [boss_xs[i] - clamp_xs[i], boss_ys[j] - clamp_ys[j]];

// ---------- Сборка ----------
module assembly() {
    color("gray", 0.6) top_shell();
    color("dimgray") bottom_lid();
    for (i = [0, 1], j = [0, 1]) color("orange")
        translate([clamp_xs[i], clamp_ys[j], clamp_zt - clamp_t])
            clamp2(clamp2_off(i, j)[0], clamp2_off(i, j)[1]);
    // макеты
    color("black") translate([mod_x0 + mod_clr, mod_y0 + mod_clr, mod_zb]) cube([mod_w, mod_d, mod_h]);
    color("silver") translate([(W - bat_w)/2, mod_y0 + 2, floor_z]) cube([bat_w, bat_d, bat_h]);
    color("blue") translate([ip_x - ip_w/2, D - wall - ip_d, lid_t + ip_rail]) cube([ip_w, ip_d, ip_pcb]);
    for (m = bay_mods) color("green") translate([m[2] - m[0]/2, m[3] - m[1]/2, floor_z]) cube([m[0], m[1], 1.6]);
    if (trk) {
        color("green") translate([teensy_x0, teensy_y0, floor_z + teensy_ledge])
            cube([teensy_w, teensy_d, teensy_pcb]);
        color("black") translate([teensy_x - 5.5, teensy_y0 - teensy_sd_out, teensy_zc - 0.4])
            cube([11, 15, 0.8]);   // карта
        color("purple") translate([aud_x0, aud_y0, aud_zb]) cube([aud_w, aud_d, aud_pcb]);
        color("black") translate([aud_jx - 3, aud_y0 - aud_jack_out, aud_zb + aud_pcb])
            cube([6, 15.3, 2*aud_jack_z]);   // гнездо
        at_wall(1) color("gold") translate([mic_x, wall + 0.3, mic_z]) rotate([-90, 0, 0])
            cylinder(d = 9.7, h = 6.7);      // капсюль 9767
        at_wall(0) color("black") translate([lin_x - jack_body/2, wall, lin_z - jack_body/2])
            cube([jack_body, 12, jack_body]);   // PJ-392 LINE
        at_wall(1) color("black") translate([jack_x - jack_body/2, wall, back_z - jack_body/2])
            cube([jack_body, 12, jack_body]);   // PJ-392 MIDI
        at_wall(1) color("red") translate([sw_x - sw_body/2, wall + 1, back_z - 4]) cube([sw_body, 10, 8]);   // MTS-102
    }
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
else if (part == "clamp2") for (i = [0, 1], j = [0, 1])
                               translate([i*25, j*30, 0]) clamp2(clamp2_off(i, j)[0], clamp2_off(i, j)[1]);
else                       assembly();
