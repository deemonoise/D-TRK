// SPDX-FileCopyrightText: 2026 deemonoise
// SPDX-License-Identifier: CERN-OHL-S-2.0
// Source location: https://github.com/deemonoise/D-TRK
// Кейкап 1u под MX-шток (Outemu, Cherry, Gateron), плоский верх. Все размеры в мм.
// Печать верхом на стол, без поддержек: шток растёт от потолка вверх.
// Рендер: openscad -o stl/keycap.stl keycap.scad
//         openscad -o stl/keycap_test.stl -D 'part="test"' keycap.scad   (пластина подбора зазора креста)
//         openscad -o stl/keycap_play.stl -D 'legend="play"' keycap.scad   (подписи: play, shift, A, B, 1…8)

part   = "cap";   // "cap" — кейкап для печати, "test" — тестовая пластина
legend = "";      // подпись, гравировка в верхней грани: "play" — треугольник, "shift" — звезда, иначе текст ("" — без)

$fn = 64;

// ---------- Кейкап ----------
cap_w   = 18;     // основание (шаг сетки 19)
cap_top = 12.7;   // верхняя грань
cap_h   = 7.4;    // высота от низа юбки до верха
cap_r   = 1.0;    // скругление углов в плане
top_t   = 1.5;    // толщина верха
wall    = 1.5;    // стенка юбки (по нормали к основанию)
edge    = 0.6;    // фаска по верхней кромке (лежит на столе — против «слоновьей ноги»)

// ---------- Шток (замерить свич) ----------
stem_d     = 5.5;   // ⌀ штока кейкапа
cross_l    = 4.0;   // крест штока свича: длина луча
cross_w    = 1.17;  //   толщина луча
cross_clr  = 0.10;  // зазор на сторону; подобран по тестовой пластине (ABS GF, Outemu Brown)
cross_dp   = 4.0;   // глубина креста от низа штока
cross_cham = 0.3;   // фаска на входе креста

// ---------- Подпись ----------
legend_h  = 2.8;    // высота букв
legend_h1 = 6.0;    // высота подписи из 1–2 символов (штрих ~0,85 — держит краску)
legend_dp = 0.6;    // глубина гравировки
play_w    = 5.6;    // треугольник ▶: ширина
play_h    = 6.5;    //   высота
star_d    = 7.4;    // звезда ✱: размах лучей
star_w    = 1.3;    //   ширина луча

// ---------- Тестовая пластина ----------
test_clr = [0.05, 0.10, 0.15, 0.20, 0.25, 0.30];
test_pitch = 9;     // шаг штоков
test_t = 1.5;       // толщина пластины (= top_t, как потолок кейкапа)

stem_l = cap_h - top_t;   // шток от потолка до низа юбки
assert(cross_dp <= stem_l - 0.6, "крест глубже штока");
assert((stem_d - cross_l) / 2 - max(test_clr) >= 0.4, "стенка штока у торцов креста тоньше 0,4");
assert(cap_w - 2*wall > 14.2, "юбка уже корпуса свича (14 мм)");
echo(str("Кейкап: ", cap_w, " x ", cap_w, " x ", cap_h, " мм, крест ", cross_l + 2*cross_clr,
         " x ", cross_w + 2*cross_clr, " на глубину ", cross_dp));

// ---------- Модель (z = 0 — верх кейкапа, растёт вниз к юбке; так и печатается) ----------
module rsquare(w, r) { offset(r) square(w - 2*r, center = true); }

// внешняя форма: усечённая пирамида со скруглёнными углами, фаска у верха
module cap_outer() {
    hull() {
        linear_extrude(0.01) rsquare(cap_top - 2*edge, cap_r);
        translate([0, 0, edge]) linear_extrude(0.01) rsquare(cap_top, cap_r);
        translate([0, 0, cap_h - 0.01]) linear_extrude(0.01) rsquare(cap_w, cap_r);
    }
}

// полость: та же пирамида, отступ wall по горизонтали (стенки почти вертикальны), от потолка до низа
module cap_inner() {
    k = (cap_w - cap_top) / 2 / (cap_h - edge);   // уширение на мм высоты
    hull() {
        translate([0, 0, top_t]) linear_extrude(0.01) rsquare(cap_top + 2*k*(top_t - edge) - 2*wall, max(cap_r - wall, 0.3));
        translate([0, 0, cap_h]) linear_extrude(0.01) rsquare(cap_w - 2*wall, max(cap_r - wall, 0.3));
        translate([0, 0, cap_h + 1]) linear_extrude(0.01) rsquare(cap_w - 2*wall + 2*k, max(cap_r - wall, 0.3));
    }
}

module cross_2d(clr) {
    square([cross_l + 2*clr, cross_w + 2*clr], center = true);
    square([cross_w + 2*clr, cross_l + 2*clr], center = true);
}

// гнездо креста: открыто в z_open (низ штока), глубина cross_dp, фаска cross_cham по толщине каждого луча
module cross_socket(z_open, clr) {
    translate([0, 0, z_open - cross_dp]) linear_extrude(cross_dp + 1) cross_2d(clr);
    for (a = [0, 90]) rotate(a) hull() {
        translate([0, 0, z_open - cross_cham]) linear_extrude(0.01)
            square([cross_l + 2*clr, cross_w + 2*clr], center = true);
        translate([0, 0, z_open]) linear_extrude(0.01)
            square([cross_l + 2*clr, cross_w + 2*clr + 2*cross_cham], center = true);   // по длине — без фаски: стенка штока у торцов
    }
}

// подпись в плане, как видна сверху
module legend_2d() {
    if (legend == "play")
        // центр масс в начале координат — на глаз стоит по центру
        offset(0.3) offset(-0.3) polygon([[-play_w/3, -play_h/2], [-play_w/3, play_h/2], [2*play_w/3, 0]]);
    else if (legend == "shift")
        for (a = [0, 60, 120]) rotate(a + 90) hull()
            for (x = [-1, 1]) translate([x * (star_d - star_w)/2, 0]) circle(d = star_w);
    else
        text(legend, size = len(legend) <= 2 ? legend_h1 : legend_h, font = "Liberation Sans:style=Bold",
             halign = "center", valign = "center");
}

module keycap() {
    difference() {
        union() {
            difference() {
                cap_outer();
                cap_inner();
            }
            translate([0, 0, top_t - 0.01]) cylinder(d = stem_d, h = stem_l + 0.01);
        }
        cross_socket(cap_h, cross_clr);
        // подпись в верхней грани, читается при виде сверху (в печати — зеркально, лицом на стол)
        if (legend != "")
            translate([0, 0, -0.01]) linear_extrude(legend_dp + 0.01) mirror([1, 0, 0]) legend_2d();
    }
}

// Тестовая пластина: штоки как в кейкапе, разные зазоры креста; номер = зазор в сотых мм
module test_plate() {
    n = len(test_clr);
    difference() {
        union() {
            translate([0, -5, 0]) linear_extrude(test_t) offset(1) offset(-1) square([n * test_pitch, 14]);
            for (i = [0 : n - 1]) translate([(i + 0.5) * test_pitch, 3.5, test_t - 0.01])
                cylinder(d = stem_d, h = stem_l + 0.01);
        }
        for (i = [0 : n - 1]) translate([(i + 0.5) * test_pitch, 3.5, 0]) {
            cross_socket(test_t + stem_l, test_clr[i]);
            translate([0, -6, test_t - 0.5]) linear_extrude(1)
                text(str(round(test_clr[i] * 100)), size = 3, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center");
        }
    }
}

if (part == "test") test_plate();
else keycap();
