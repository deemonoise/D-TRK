// SPDX-FileCopyrightText: 2026 deemonoise
// SPDX-License-Identifier: CERN-OHL-S-2.0
// Source location: https://github.com/deemonoise/D-TRK
// Кноб энкодера EC11 (D-вал) с накаткой, фаской 3 мм и насечками на ней. Все размеры в мм.
// Снизу выборка под гайку и резьбовую втулку — низ кноба висит над панелью на `gap`.
// Рендер: openscad -o stl/knob.stl knob.scad   (печать верхом на стол, без поддержек)

part = "print";   // "print" — для печати, "view" — как стоит на панели

$fn = 96;

// ---------- Кноб ----------
knob_d   = 30;
knurl_n  = round(PI * knob_d / 2.6);   // канавок в каждую сторону (шаг ~2,6)
knurl_dp = 0.8;     // глубина канавки
knurl_top = 0.4;    // ширина площадки ромба / шаг
knurl_ang = 30;     // угол винтовой канавки к оси
skin     = 1.5;     // толщина крышки над концом вала
chamfer  = 3;       // фаска 45° по верхней кромке
cham_n   = knurl_n; // насечек на фаске (радиальные V-канавки)

// ---------- Энкодер (замерить) ----------
shaft_d   = 6.0;    // ⌀ вала
shaft_dd  = 4.5;    // D-вал: от лыски до противоположной стороны
flat_l    = 10;     // длина лыски от конца вала
shaft_clr = 0.15;   // зазор посадки (на диаметр); туго — увеличить
shaft_l   = 20;     // от опорной плоскости корпуса энкодера до конца вала
bush_d    = 7;      // резьбовая втулка M7
bush_l    = 7;      // высота втулки от опорной плоскости
nut_af    = 10;     // гайка под ключ
nut_h     = 2.0;
washer_t  = 0.5;    // шайба (0 — без шайбы)
panel_t   = 2.5;    // толщина панели (top_t в case.scad)
push      = 0.5;    // ход кнопки энкодера

gap = push + 0.5;   // зазор низ кноба — панель (в нажатом положении остаётся 0,5)

// ---------- Производные (z от верха панели) ----------
bush_top  = bush_l - panel_t;
nut_top   = washer_t + nut_h;
shaft_top = shaft_l - panel_t;
knob_h    = shaft_top - gap + skin;

nut_cav_d  = nut_af / cos(30) + 1.0;   // по углам гайки + запас
nut_cav_h  = nut_top + 0.5 - gap;
bush_cav_d = bush_d + 1.0;
bush_cav_h = bush_top + 0.5 - gap;
bore_h     = knob_h - skin;

echo(str("Кноб: ⌀", knob_d, " x ", knob_h, " мм, зазор до панели ", gap,
         " мм, вал в кнобе ", bore_h - bush_cav_h, " мм"));
assert(bore_h - bush_cav_h >= 5, "вал заходит в кноб меньше 5 мм");
assert((knob_d - nut_cav_d) / 2 - knurl_dp >= 1.2, "стенка у гайки тоньше 1,2 мм");

assert(chamfer < knob_h - 2, "фаска выше кноба");

// ---------- Модель ----------
module d_profile(d, dd) {
    intersection() {
        circle(d = d);
        translate([-d/2, -d/2]) square([d, dd]);   // лыска сверху по +Y
    }
}

knurl_p = PI * knob_d / knurl_n;                         // шаг по окружности
knurl_tw = knob_h * tan(knurl_ang) / (knob_d/2) * 180 / PI;  // закрутка за высоту, °

// круг с V-канавками по окружности
module knurl_2d() {
    w = knurl_p * (1 - knurl_top);     // ширина канавки на поверхности
    r = knob_d / 2;
    k = (knurl_dp + 0.5) / knurl_dp;   // продлить стенки V за пределы круга
    difference() {
        circle(d = knob_d);
        for (i = [0 : knurl_n - 1]) rotate(i * 360 / knurl_n)
            polygon([[r - knurl_dp, 0], [r + 0.5, -w/2 * k], [r + 0.5, w/2 * k]]);
    }
}

// V-канавка вдоль образующей фаски: локальные x — нормаль к фаске, y — по касательной, z — вверх по фаске
module cham_groove() {
    s = 1 / sqrt(2);
    r = knob_d / 2;
    w = knurl_p * (1 - knurl_top);
    k = (knurl_dp + 0.5) / knurl_dp;
    multmatrix([[s, 0, -s, r + s], [0, 1, 0, 0], [s, 0, s, knob_h - chamfer - s], [0, 0, 0, 1]])
        linear_extrude(chamfer * sqrt(2) + 2)
            polygon([[-knurl_dp, 0], [0.5, -w/2 * k], [0.5, w/2 * k]]);
}

module knob() {
    difference() {
        intersection() {
            // две встречные винтовые накатки — пересечение даёт ромбы
            linear_extrude(knob_h, twist =  knurl_tw, slices = 40) knurl_2d();
            linear_extrude(knob_h, twist = -knurl_tw, slices = 40) knurl_2d();
            // фаска
            union() {
                cylinder(d = knob_d + 2, h = knob_h - chamfer);
                translate([0, 0, knob_h - chamfer - 0.01])
                    cylinder(d1 = knob_d, d2 = knob_d - 2*chamfer, h = chamfer + 0.01);
            }
        }
        // насечки на фаске
        for (i = [0 : cham_n - 1]) rotate(i * 360 / cham_n) cham_groove();
        // выборки снизу: гайка, втулка
        translate([0, 0, -0.01]) cylinder(d = nut_cav_d, h = nut_cav_h + 0.01);
        translate([0, 0, -0.01]) cylinder(d = bush_cav_d, h = bush_cav_h + 0.01);
        // вал: круглая часть, выше — D
        translate([0, 0, -0.01]) cylinder(d = shaft_d + shaft_clr, h = bore_h - flat_l + 0.01);
        translate([0, 0, bore_h - flat_l - 0.01])
            linear_extrude(flat_l + 0.01) d_profile(shaft_d + shaft_clr, shaft_dd + shaft_clr);
    }
}

if (part == "print") translate([0, 0, knob_h]) rotate([180, 0, 0]) knob();
else {
    // панель и энкодер для проверки
    color("gray") translate([-25, -25, -panel_t]) difference() {
        cube([50, 50, panel_t]);
        translate([25, 25, -1]) cylinder(d = 7.2, h = panel_t + 2);
    }
    color("silver") {
        cylinder(d = nut_af / cos(30), h = nut_top, $fn = 6);
        translate([0, 0, -panel_t]) cylinder(d = bush_d, h = bush_l);
        translate([0, 0, -panel_t]) cylinder(d = shaft_d, h = shaft_l);
    }
    color("orange", 0.6) translate([0, 0, gap]) knob();
}
