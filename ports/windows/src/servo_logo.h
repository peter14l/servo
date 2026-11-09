/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

// Modern high-tech aerodynamic Windows 11 Fluent 2 Servo Logo SVG
inline const char* GetServoLogoSvg(int size = 48) {
    static char buf[2048];
    snprintf(buf, sizeof(buf),
        "<svg width=\"%d\" height=\"%d\" viewBox=\"0 0 64 64\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <defs>\n"
        "    <linearGradient id=\"servo_grad_core\" x1=\"8\" y1=\"8\" x2=\"56\" y2=\"56\" gradientUnits=\"userSpaceOnUse\">\n"
        "      <stop offset=\"0%%\" stop-color=\"#00D2FF\"/>\n"
        "      <stop offset=\"50%%\" stop-color=\"#0066E0\"/>\n"
        "      <stop offset=\"100%%\" stop-color=\"#0A1E5A\"/>\n"
        "    </linearGradient>\n"
        "    <linearGradient id=\"servo_swoop_grad\" x1=\"12\" y1=\"20\" x2=\"52\" y2=\"44\" gradientUnits=\"userSpaceOnUse\">\n"
        "      <stop offset=\"0%%\" stop-color=\"#FFFFFF\" stop-opacity=\"0.95\"/>\n"
        "      <stop offset=\"100%%\" stop-color=\"#60CDFF\" stop-opacity=\"0.6\"/>\n"
        "    </linearGradient>\n"
        "    <filter id=\"servo_ambient_glow\" x=\"0\" y=\"0\" width=\"64\" height=\"64\" filterUnits=\"userSpaceOnUse\">\n"
        "      <feGaussianBlur stdDeviation=\"2\" result=\"blur\"/>\n"
        "      <feComposite in=\"SourceGraphic\" in2=\"blur\" operator=\"over\"/>\n"
        "    </filter>\n"
        "  </defs>\n"
        "  <!-- Ambient Glow Ring -->\n"
        "  <circle cx=\"32\" cy=\"32\" r=\"29\" fill=\"none\" stroke=\"#0078D4\" stroke-width=\"1.5\" stroke-opacity=\"0.35\" filter=\"url(#servo_ambient_glow)\"/>\n"
        "  <!-- Main Gradient Orb -->\n"
        "  <circle cx=\"32\" cy=\"32\" r=\"26\" fill=\"url(#servo_grad_core)\"/>\n"
        "  <!-- Inner Speed Ring -->\n"
        "  <path d=\"M14 34C15 22 24 14 36 14C45 14 51 20 51 29\" stroke=\"url(#servo_swoop_grad)\" stroke-width=\"3.2\" stroke-linecap=\"round\"/>\n"
        "  <path d=\"M18 42C23 48 33 50 42 46C47 43 50 38 50 34\" stroke=\"#00D2FF\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-opacity=\"0.8\"/>\n"
        "  <!-- Modern Dynamic 'S' Aperture -->\n"
        "  <circle cx=\"37\" cy=\"25\" r=\"5\" fill=\"#FFFFFF\" fill-opacity=\"0.95\"/>\n"
        "  <circle cx=\"27\" cy=\"39\" r=\"4\" fill=\"#D2F5FF\" fill-opacity=\"0.95\"/>\n"
        "  <path d=\"M34 27L25 36L29 41L39 30Z\" fill=\"#FFFFFF\" fill-opacity=\"0.85\"/>\n"
        "  <!-- Rim highlight -->\n"
        "  <circle cx=\"32\" cy=\"32\" r=\"25.5\" stroke=\"#FFFFFF\" stroke-opacity=\"0.4\" stroke-width=\"1\"/>\n"
        "</svg>\n", size, size);
    return buf;
}
