#include "platform.h"
#include "sky.h"

#include "math_core.h"          // Vec3, kPi, clamp01, smoothstep01, compute_daylight, compute_night_alpha
#include "noise.h"              // lerp, perlin
#include "camera.h"             // Camera3D, g_camera
#include "config_types.h"       // SkyConfig (type of g_sky_cfg below)
#include "items_particles.h"    // ShootingStar (type only - g_shooting_stars itself is NOT
                                 // declared extern there, see comment below)
#include "game_state.h"         // rng_next_u32, rng_next_f01

#include <algorithm>
#include <cmath>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp - nao
// fazem parte desta etapa de extracao). g_day_time/g_sky_cfg ja eram nao-static em main.cpp
// (g_day_time desde o estagio modules_building; g_sky_cfg desde sempre, config_io.cpp ja usa
// esta mesma extern) - so precisamos da declaracao aqui tambem, mesmo padrao de
// g_physics_cfg em camera.cpp / g_terrain_cfg em world.cpp. g_shooting_stars (o vetor,
// std::vector<ShootingStar>) ja era nao-static em main.cpp antes desta etapa (perdeu
// "static" para save_load.cpp's load_game()), mas nenhum header ainda o declarava extern -
// items_particles.h so define o tipo ShootingStar, nao a variavel. Precisamos da nossa
// propria declaracao aqui, mesmo padrao das demais globais desta secao.
extern float g_day_time;
extern SkyConfig g_sky_cfg;
extern std::vector<ShootingStar> g_shooting_stars;

// kDayLength: own copy of the same compile-time literal main.cpp keeps (and lighting.cpp
// too) - not shared via extern since it's compile-time state, not mutable, same pattern as
// kDayLength in modules_building.cpp/minimap.cpp.
static constexpr float kDayLength = 150.0f; // seconds

// ============= ALIEN SKY SYSTEM (esferas reais + parallax) =============

static float hash01(float v) {
    float h = std::sin(v * 12.9898f + 78.233f) * 43758.5453f;
    return std::fmod(std::fabs(h), 1.0f);
}

SkyPalette compute_sky_palette(float day_phase, float atmos_factor) {
    float daylight = compute_daylight(day_phase);
    float night = compute_night_alpha(day_phase);
    float sun_warm = smoothstep01(0.05f, 0.45f, daylight) * (1.0f - smoothstep01(0.75f, 1.0f, daylight));
    float atmos = clamp01(atmos_factor);

    SkyPalette p{};
    float night_hz_r = 0.05f, night_hz_g = 0.06f, night_hz_b = 0.11f;
    float night_zn_r = 0.02f, night_zn_g = 0.03f, night_zn_b = 0.07f;
    float day_hz_r = lerp(0.48f, 0.36f, atmos);
    float day_hz_g = lerp(0.37f, 0.52f, atmos);
    float day_hz_b = lerp(0.25f, 0.70f, atmos);
    float day_zn_r = lerp(0.18f, 0.19f, atmos);
    float day_zn_g = lerp(0.23f, 0.38f, atmos);
    float day_zn_b = lerp(0.35f, 0.74f, atmos);

    p.hz_r = lerp(night_hz_r, day_hz_r, daylight);
    p.hz_g = lerp(night_hz_g, day_hz_g, daylight);
    p.hz_b = lerp(night_hz_b, day_hz_b, daylight);
    p.zn_r = lerp(night_zn_r, day_zn_r, daylight);
    p.zn_g = lerp(night_zn_g, day_zn_g, daylight);
    p.zn_b = lerp(night_zn_b, day_zn_b, daylight);

    p.hz_r += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.32f;
    p.hz_g += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.16f;
    p.hz_b += sun_warm * g_sky_cfg.atmosphere_horizon_boost * 0.07f;

    p.zn_r += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.05f;
    p.zn_g += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.11f;
    p.zn_b += daylight * g_sky_cfg.atmosphere_zenith_boost * 0.18f;

    // Horizon fade at night for more depth.
    float fade = night * g_sky_cfg.horizon_fade;
    p.hz_r = lerp(p.hz_r, p.zn_r, fade * 0.45f);
    p.hz_g = lerp(p.hz_g, p.zn_g, fade * 0.45f);
    p.hz_b = lerp(p.hz_b, p.zn_b, fade * 0.45f);

    p.hz_r = clamp01(p.hz_r); p.hz_g = clamp01(p.hz_g); p.hz_b = clamp01(p.hz_b);
    p.zn_r = clamp01(p.zn_r); p.zn_g = clamp01(p.zn_g); p.zn_b = clamp01(p.zn_b);
    return p;
}

static void render_sky_gradient_dome(float cam_x, float cam_z, const SkyPalette& p) {
    constexpr int kRings = 18;
    constexpr int kSegs = 64;
    constexpr float kRadius = 1850.0f;
    constexpr float kBaseY = -120.0f;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    for (int ring = 0; ring < kRings; ++ring) {
        float t0 = (float)ring / (float)kRings;
        float t1 = (float)(ring + 1) / (float)kRings;
        float e0 = t0 * (kPi * 0.5f);
        float e1 = t1 * (kPi * 0.5f);
        float y0 = kBaseY + std::sin(e0) * kRadius;
        float y1 = kBaseY + std::sin(e1) * kRadius;
        float r0 = std::cos(e0) * kRadius;
        float r1 = std::cos(e1) * kRadius;

        float c0 = smoothstep01(0.0f, 1.0f, t0);
        float c1 = smoothstep01(0.0f, 1.0f, t1);
        float c0r = lerp(p.hz_r, p.zn_r, c0);
        float c0g = lerp(p.hz_g, p.zn_g, c0);
        float c0b = lerp(p.hz_b, p.zn_b, c0);
        float c1r = lerp(p.hz_r, p.zn_r, c1);
        float c1g = lerp(p.hz_g, p.zn_g, c1);
        float c1b = lerp(p.hz_b, p.zn_b, c1);

        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= kSegs; ++i) {
            float a = (float)i / (float)kSegs * 2.0f * kPi;
            float ca = std::cos(a);
            float sa = std::sin(a);
            glColor4f(c1r, c1g, c1b, 1.0f);
            glVertex3f(cam_x + ca * r1, y1, cam_z + sa * r1);
            glColor4f(c0r, c0g, c0b, 1.0f);
            glVertex3f(cam_x + ca * r0, y0, cam_z + sa * r0);
        }
        glEnd();
    }
}

static void render_billboard_disc(const Vec3& center, float radius, float r, float g, float b, float a, int segments = 28) {
    Vec3 to_cam = vec3_sub(g_camera.position, center);
    if (vec3_length(to_cam) < 0.001f) to_cam = {0.0f, 0.0f, 1.0f};
    to_cam = vec3_normalize(to_cam);
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 right = vec3_cross(up, to_cam);
    if (vec3_length(right) < 0.001f) right = {1.0f, 0.0f, 0.0f};
    right = vec3_normalize(right);
    Vec3 disc_up = vec3_normalize(vec3_cross(to_cam, right));

    glBegin(GL_TRIANGLE_FAN);
    glColor4f(r, g, b, a);
    glVertex3f(center.x, center.y, center.z);
    for (int i = 0; i <= segments; ++i) {
        float ang = (float)i / (float)segments * 2.0f * kPi;
        float ca = std::cos(ang);
        float sa = std::sin(ang);
        Vec3 p = vec3_add(center, vec3_add(vec3_scale(right, ca * radius), vec3_scale(disc_up, sa * radius)));
        glColor4f(r, g, b, 0.0f);
        glVertex3f(p.x, p.y, p.z);
    }
    glEnd();
}

static void render_lit_sphere(const Vec3& center, float radius, const Vec3& light_dir, const Vec3& view_pos,
                              float base_r, float base_g, float base_b, float alpha,
                              float ambient, float diffuse_mul, float spec_mul,
                              float noise_freq = 0.0f, float noise_amp = 0.0f,
                              int lat_seg = 18, int lon_seg = 24) {
    Vec3 ldir = vec3_normalize(light_dir);
    for (int lat = 0; lat < lat_seg; ++lat) {
        float v0 = -0.5f + (float)lat / (float)lat_seg;
        float v1 = -0.5f + (float)(lat + 1) / (float)lat_seg;
        float p0 = v0 * kPi;
        float p1 = v1 * kPi;
        float y0 = std::sin(p0);
        float y1 = std::sin(p1);
        float r0 = std::cos(p0);
        float r1 = std::cos(p1);

        glBegin(GL_QUAD_STRIP);
        for (int lon = 0; lon <= lon_seg; ++lon) {
            float u = (float)lon / (float)lon_seg * 2.0f * kPi;
            float cu = std::cos(u);
            float su = std::sin(u);

            auto emit = [&](float rr, float yy) {
                Vec3 n = vec3_normalize({cu * rr, yy, su * rr});
                Vec3 p = vec3_add(center, vec3_scale(n, radius));
                float ndl = std::max(0.0f, vec3_dot(n, ldir));
                Vec3 vdir = vec3_normalize(vec3_sub(view_pos, p));
                Vec3 h = vec3_normalize(vec3_add(ldir, vdir));
                float spec = std::pow(std::max(0.0f, vec3_dot(n, h)), 26.0f) * spec_mul;
                float nvar = 0.0f;
                if (noise_freq > 0.00001f) {
                    nvar = (perlin(p.x * noise_freq + 133.0f, p.z * noise_freq + 617.0f) - 0.5f) * noise_amp;
                }
                float lit = std::max(0.0f, ambient + ndl * diffuse_mul + nvar);
                float cr = clamp01(base_r * lit + spec);
                float cg = clamp01(base_g * lit + spec * 0.95f);
                float cb = clamp01(base_b * lit + spec * 0.90f);
                glColor4f(cr, cg, cb, alpha);
                glVertex3f(p.x, p.y, p.z);
            };

            emit(r1, y1);
            emit(r0, y0);
        }
        glEnd();
    }
}

static void render_star_layer(float cam_x, float cam_z, float day_phase, float night_alpha) {
    if (night_alpha < 0.03f) return;
    int star_count = (int)std::lround(g_sky_cfg.stars_density);
    star_count = std::clamp(star_count, 120, 4000);

    float origin_x = cam_x * g_sky_cfg.stars_parallax;
    float origin_z = cam_z * g_sky_cfg.stars_parallax;

    glPointSize(1.4f);
    glBegin(GL_POINTS);
    for (int i = 0; i < star_count; ++i) {
        float u = hash01((float)i * 1.11f + 13.0f);
        float v = hash01((float)i * 1.71f + 31.0f);
        float w = hash01((float)i * 2.47f + 79.0f);
        float theta = u * 2.0f * kPi;
        float y01 = 0.22f + v * 0.76f;
        float rr = std::sqrt(std::max(0.0f, 1.0f - y01 * y01));
        float dist = 1300.0f + w * 900.0f;
        float sx = origin_x + std::cos(theta) * rr * dist;
        float sy = 190.0f + y01 * 980.0f;
        float sz = origin_z + std::sin(theta) * rr * dist;
        float twinkle = 0.45f + 0.55f * std::sin((float)i * 0.37f + day_phase * 12.0f);
        float a = night_alpha * twinkle * 0.9f;
        float sr = 0.82f + 0.16f * u;
        float sg = 0.82f + 0.16f * v;
        float sb = 0.90f + 0.10f * w;
        glColor4f(sr, sg, sb, a);
        glVertex3f(sx, sy, sz);
    }
    glEnd();
    glPointSize(1.0f);
}

static void render_nebula_layer(float cam_x, float cam_z, float day_phase, float night_alpha) {
    float alpha = night_alpha * g_sky_cfg.nebula_alpha;
    if (alpha < 0.01f) return;
    float origin_x = cam_x * g_sky_cfg.nebula_parallax;
    float origin_z = cam_z * g_sky_cfg.nebula_parallax;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int i = 0; i < 5; ++i) {
        float u = hash01((float)i * 9.3f + 21.0f);
        float v = hash01((float)i * 17.7f + 55.0f);
        float ang = day_phase * 0.35f + u * 2.0f * kPi;
        Vec3 c = {
            origin_x + std::cos(ang) * (900.0f + 420.0f * u),
            260.0f + 260.0f * v,
            origin_z + std::sin(ang) * (780.0f + 380.0f * v)
        };
        float rad = 220.0f + 170.0f * u;
        float nr = 0.30f + 0.30f * u;
        float ng = 0.18f + 0.28f * v;
        float nb = 0.42f + 0.32f * (1.0f - u);
        render_billboard_disc(c, rad, nr, ng, nb, alpha * (0.25f + 0.35f * v), 34);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void render_cloud_layer(float cam_x, float cam_z, float day_phase, float atmos_factor) {
    float alpha = g_sky_cfg.cloud_alpha * (0.35f + atmos_factor * 0.65f);
    if (alpha < 0.01f) return;
    float origin_x = cam_x * g_sky_cfg.cloud_parallax;
    float origin_z = cam_z * g_sky_cfg.cloud_parallax;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < 6; ++i) {
        float t = (float)i * 1.71f;
        float u = hash01(t + 17.0f);
        float v = hash01(t + 63.0f);
        float spin = day_phase * 1.8f + u * 2.0f * kPi;
        Vec3 c = {
            origin_x + std::cos(spin) * (460.0f + 380.0f * u),
            320.0f + 160.0f * v,
            origin_z + std::sin(spin) * (420.0f + 320.0f * v)
        };
        float rad = 130.0f + 110.0f * u;
        render_billboard_disc(c, rad, 0.88f, 0.90f, 0.94f, alpha * (0.35f + 0.30f * v), 30);
    }
}

void update_shooting_stars(float dt, float day_phase) {
    for (auto& s : g_shooting_stars) {
        s.life -= dt;
        s.offset = vec3_add(s.offset, vec3_scale(s.vel, dt));
    }
    g_shooting_stars.erase(
        std::remove_if(g_shooting_stars.begin(), g_shooting_stars.end(),
            [](const ShootingStar& s) { return s.life <= 0.0f; }),
        g_shooting_stars.end());

    float night_alpha = compute_night_alpha(day_phase);
    if (night_alpha < 0.55f) return;
    if (g_shooting_stars.size() >= 4) return;

    float spawn_rate = 0.05f + 0.12f * (night_alpha - 0.55f);
    if (rng_next_f01() > dt * spawn_rate) return;

    ShootingStar s;
    s.max_life = 0.75f + rng_next_f01() * 0.55f;
    s.life = s.max_life;
    s.length = 120.0f + rng_next_f01() * 180.0f;
    float start_radius = 1100.0f + rng_next_f01() * 450.0f;
    float start_ang = rng_next_f01() * 2.0f * kPi;
    s.offset.x = std::cos(start_ang) * start_radius;
    s.offset.z = std::sin(start_ang) * start_radius;
    s.offset.y = 420.0f + rng_next_f01() * 520.0f;
    float dir_ang = start_ang + (0.90f + rng_next_f01() * 0.60f) * (((rng_next_u32() & 1u) != 0u) ? 1.0f : -1.0f);
    float spd = 650.0f + rng_next_f01() * 450.0f;
    s.vel.x = std::cos(dir_ang) * spd;
    s.vel.z = std::sin(dir_ang) * spd;
    s.vel.y = -(120.0f + rng_next_f01() * 260.0f);
    float tint = 0.86f + rng_next_f01() * 0.14f;
    s.r = tint;
    s.g = tint;
    s.b = 0.95f + rng_next_f01() * 0.05f;
    g_shooting_stars.push_back(s);
}

static void render_shooting_stars(float cam_x, float cam_y, float cam_z, float night_alpha) {
    if (night_alpha < 0.20f) return;
    if (g_shooting_stars.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (const auto& s : g_shooting_stars) {
        float progress = 1.0f - (s.life / std::max(0.001f, s.max_life));
        float fade_in = smoothstep01(0.00f, 0.12f, progress);
        float fade_out = 1.0f - smoothstep01(0.70f, 1.00f, progress);
        float a = night_alpha * fade_in * fade_out;
        if (a <= 0.01f) continue;
        Vec3 head = {cam_x + s.offset.x, s.offset.y, cam_z + s.offset.z};
        Vec3 dir = vec3_normalize(s.vel);
        Vec3 tail = vec3_sub(head, vec3_scale(dir, s.length));
        glColor4f(s.r, s.g, s.b, a);
        glVertex3f(tail.x, tail.y, tail.z);
        glVertex3f(head.x, head.y, head.z);
    }
    glEnd();
    glLineWidth(1.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Funcao principal para renderizar todo o ceu alienigena
void render_alien_sky(float cam_x, float cam_y, float cam_z, float day_phase, float atmos_factor) {
    float night_alpha = compute_night_alpha(day_phase);
    SkyPalette palette = compute_sky_palette(day_phase, atmos_factor);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);

    render_sky_gradient_dome(cam_x, cam_z, palette);
    render_star_layer(cam_x, cam_z, day_phase, night_alpha);
    render_nebula_layer(cam_x, cam_z, day_phase, night_alpha);

    Vec3 camera_ref = {cam_x, cam_y, cam_z};
    float sun_angle = day_phase * 2.0f * kPi - kPi * 0.5f;
    Vec3 sun_pos = {
        cam_x + std::cos(sun_angle) * g_sky_cfg.sun_distance,
        85.0f + std::sin(sun_angle) * 315.0f,
        cam_z - 200.0f + std::sin(sun_angle * 0.5f) * 100.0f
    };
    Vec3 sun_dir = vec3_normalize(vec3_sub(sun_pos, camera_ref));

    // Movimento muito lento dos astros (em "dias" de jogo), evitando deslocamento brusco no ceu.
    float sky_days = g_day_time / kDayLength;
    float planet_phase = sky_days * std::max(0.0f, g_sky_cfg.planet_orbit_speed) * 0.10f;
    float planet_orbit = planet_phase * 2.0f * kPi;
    float planet_az = 2.25f + std::sin(planet_orbit) * 0.35f;
    float planet_el = 0.30f + std::sin(planet_orbit * 0.43f + 0.9f) * 0.08f;
    auto body_from_spherical = [&](float az, float el, float dist, float parallax) -> Vec3 {
        float cos_el = std::cos(el);
        return {
            cam_x * parallax + std::cos(az) * cos_el * dist,
            70.0f + std::sin(el) * dist,
            cam_z * parallax + std::sin(az) * cos_el * dist
        };
    };
    Vec3 planet_pos = body_from_spherical(planet_az, planet_el, g_sky_cfg.planet_distance, g_sky_cfg.planet_parallax);

    // Nao permitir que o planeta principal cruze visualmente o disco do sol.
    Vec3 planet_dir = vec3_normalize(vec3_sub(planet_pos, camera_ref));
    if (vec3_dot(planet_dir, sun_dir) > 0.66f) {
        planet_az += (sun_dir.x >= 0.0f) ? -0.95f : 0.95f;
        planet_pos = body_from_spherical(planet_az, planet_el, g_sky_cfg.planet_distance, g_sky_cfg.planet_parallax);
    }
    render_lit_sphere(planet_pos, g_sky_cfg.planet_radius, sun_dir, g_camera.position,
                      0.20f, 0.28f, 0.42f, 0.98f,
                      0.22f, 0.90f, 0.18f,
                      0.010f, 0.22f, 20, 28);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    render_billboard_disc(planet_pos, g_sky_cfg.planet_radius * 1.45f, 0.46f, 0.60f, 0.90f, night_alpha * 0.16f, 34);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float moon_phase = sky_days * std::max(0.0f, g_sky_cfg.moon_orbit_speed) * 0.08f;
    float moon2_phase = sky_days * std::max(0.0f, g_sky_cfg.moon2_orbit_speed) * 0.10f;
    float moon_a1 = moon_phase * 2.0f * kPi + 1.1f;
    float moon_a2 = moon2_phase * 2.0f * kPi + 2.7f;
    Vec3 moon1_pos = body_from_spherical(1.7f + std::sin(moon_a1) * 0.75f,
                                         0.38f + std::sin(moon_a1 * 0.83f) * 0.15f,
                                         g_sky_cfg.moon_distance,
                                         g_sky_cfg.moon_parallax);
    Vec3 moon2_pos = body_from_spherical(2.5f + std::sin(moon_a2) * 0.92f,
                                         0.46f + std::sin(moon_a2 * 0.71f) * 0.13f,
                                         g_sky_cfg.moon2_distance,
                                         g_sky_cfg.moon2_parallax);

    auto avoid_sun_cross = [&](Vec3& pos, float min_dot, float yaw_shift) {
        Vec3 dir = vec3_normalize(vec3_sub(pos, camera_ref));
        if (vec3_dot(dir, sun_dir) <= min_dot) return;
        float vx = pos.x - camera_ref.x;
        float vz = pos.z - camera_ref.z;
        float r = std::sqrt(vx * vx + vz * vz);
        if (r < 0.001f) return;
        float yaw = std::atan2(vz, vx) + yaw_shift;
        pos.x = camera_ref.x + std::cos(yaw) * r;
        pos.z = camera_ref.z + std::sin(yaw) * r;
    };
    avoid_sun_cross(moon1_pos, 0.84f, (sun_dir.x >= 0.0f) ? -0.65f : 0.65f);
    avoid_sun_cross(moon2_pos, 0.84f, (sun_dir.x >= 0.0f) ? 0.75f : -0.75f);

    float moon_alpha = 0.35f + night_alpha * 0.65f;
    render_lit_sphere(moon1_pos, g_sky_cfg.moon_radius, sun_dir, g_camera.position,
                      0.64f, 0.58f, 0.54f, moon_alpha,
                      0.12f, 0.95f, 0.10f,
                      0.030f, 0.30f, 16, 22);
    render_lit_sphere(moon2_pos, g_sky_cfg.moon2_radius, sun_dir, g_camera.position,
                      0.58f, 0.68f, 0.82f, moon_alpha * 0.92f,
                      0.12f, 0.95f, 0.14f,
                      0.045f, 0.24f, 14, 20);

    float eclipse_cycle = 0.5f + 0.5f * std::sin((g_day_time / (kDayLength * g_sky_cfg.eclipse_frequency_days)) * 2.0f * kPi);
    float sun_align = vec3_dot(vec3_normalize(vec3_sub(moon1_pos, camera_ref)), sun_dir);
    float eclipse = smoothstep01(0.996f, 0.9998f, sun_align) * smoothstep01(0.78f, 1.0f, eclipse_cycle) * g_sky_cfg.eclipse_strength;

    if (sun_pos.y > 40.0f) {
        float sun_alpha = 1.0f - eclipse;
        render_lit_sphere(sun_pos, g_sky_cfg.sun_radius, sun_dir, g_camera.position,
                          1.0f, 0.84f, 0.50f, sun_alpha,
                          0.95f, 0.55f, 0.05f,
                          0.0f, 0.0f, 18, 24);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        float halo_mul = g_sky_cfg.sun_halo_size;
        render_billboard_disc(sun_pos, g_sky_cfg.sun_radius * halo_mul, 1.0f, 0.70f, 0.35f, (0.12f + 0.20f * g_sky_cfg.bloom_intensity) * sun_alpha, 34);
        render_billboard_disc(sun_pos, g_sky_cfg.sun_radius * (halo_mul * 1.8f), 1.0f, 0.52f, 0.22f, (0.05f + 0.10f * g_sky_cfg.bloom_intensity) * sun_alpha, 34);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    render_cloud_layer(cam_x, cam_z, day_phase, atmos_factor);
    render_shooting_stars(cam_x, cam_y, cam_z, night_alpha);

    glEnable(GL_DEPTH_TEST);
}
