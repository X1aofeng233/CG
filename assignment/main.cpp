#include <windows.h>
#include <GL/glut.h>
#include <cmath>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cwchar>

#include "stb_image.h"

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

static float g_rotX = 28.0f;
static float g_rotY = -32.0f;
static bool g_explodedCollapsed = false;
static float g_explodedAnim = 0.0f;
static float g_explodedTarget = 0.0f;
static const int g_refreshMills = 16;

// Demo animation variables
static bool demoStarted = false;
static float demoTime = 0.0f;
static float glucoseValue = 90.0f;
static int watchHour = 8;
static int watchMinute = 0;
static int demoStage = 0;  // 0 = idle, 1 = glucose rising, 2 = signal, 3 = injection/falling, 4 = recovered
static float demoInjectionTime = 0.0f;
static float signalT = 0.0f;        // signal point progress on Bezier curve
static bool signalActive = false;   // whether signal curve is active
static float g_scale = 1.0f;

// Stage 3: injection particle animation
struct InjectionParticle
{
    float life;
    float speed;
    float offsetX;
    float offsetZ;
    float radius;
    bool active;
};

static bool injectionActive = false;
static bool injectionParticlesVisible = false;
static const int INJECTION_PARTICLE_COUNT = 80;
static InjectionParticle injectionParticles[INJECTION_PARTICLE_COUNT];

static GLUquadric* g_quad = nullptr;
static GLuint grayPatchTexture = 0;
static GLuint deepGrayPatchTexture = 0;
static GLuint backgroundTexture = 0;

static const float STAGE_WIDTH = 1000.0f;
static const float STAGE_DEPTH = 900.0f;
static const float STAGE_HEIGHT = 2.0f;
static const float STAGE_CENTER_Y = -100.0f;
static const float STAGE_TOP_Y = STAGE_CENTER_Y + STAGE_HEIGHT * 0.5f + 0.08f;
static GLfloat stageTopPlane[4] = { 0.0f, 1.0f, 0.0f, -STAGE_TOP_Y };
static GLfloat stagePlaneVertices[4][3] = {
    { -STAGE_WIDTH * 0.5f, STAGE_TOP_Y,  STAGE_DEPTH * 0.5f },
    {  STAGE_WIDTH * 0.5f, STAGE_TOP_Y,  STAGE_DEPTH * 0.5f },
    {  STAGE_WIDTH * 0.5f, STAGE_TOP_Y, -STAGE_DEPTH * 0.5f },
    { -STAGE_WIDTH * 0.5f, STAGE_TOP_Y, -STAGE_DEPTH * 0.5f }
};
static GLfloat stageShadowMatrix[4][4];
static GLfloat shadowLightPosition[4] = { 80.0f, 100.0f, 150.0f, 1.0f };
static bool g_renderingShadow = false;
static const float STAGE_STENCIL_WIDTH = STAGE_WIDTH * 1.01f;
static const float STAGE_STENCIL_DEPTH = STAGE_DEPTH * 1.01f;

static Vec3 Sub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

// Cross product: gets a direction perpendicular to two vectors, used for normals.
static Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// Normalize keeps only the direction of a vector and changes its length to 1.
static Vec3 Normalize(const Vec3& v)
{
    // Euclidean length: sqrt(x^2 + y^2 + z^2).
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) return { 0, 0, 1 };
    return { v.x / len, v.y / len, v.z / len };
}

static void findPlane(GLfloat plane[4], const GLfloat v0[3], const GLfloat v1[3], const GLfloat v2[3])
{
    // Build two edges on the stage surface from three known points.
    GLfloat vec0[3] = {
        v1[0] - v0[0],
        v1[1] - v0[1],
        v1[2] - v0[2]
    };
    GLfloat vec1[3] = {
        v2[0] - v0[0],
        v2[1] - v0[1],
        v2[2] - v0[2]
    };

    // Cross product of the two edges gives the plane normal (A, B, C).
    plane[0] = vec0[1] * vec1[2] - vec0[2] * vec1[1];
    plane[1] = vec0[2] * vec1[0] - vec0[0] * vec1[2];
    plane[2] = vec0[0] * vec1[1] - vec0[1] * vec1[0];

    // Normalize the normal so lighting/shadow math is stable.
    GLfloat len = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
    if (len > 1e-6f)
    {
        plane[0] /= len;
        plane[1] /= len;
        plane[2] /= len;
    }

    // Plane equation is Ax + By + Cz + D = 0, so D is solved from one point.
    plane[3] = -(plane[0] * v0[0] + plane[1] * v0[1] + plane[2] * v0[2]);
}

// Builds a planar projection matrix for shadows cast onto stageTopPlane.
static void shadowMatrix(GLfloat shadowMat[4][4], GLfloat groundplane[4], GLfloat lightpos[4])
{
    // Dot product between the light position and plane controls projection scale.
    GLfloat dot = groundplane[0] * lightpos[0] +
                  groundplane[1] * lightpos[1] +
                  groundplane[2] * lightpos[2] +
                  groundplane[3] * lightpos[3];

    // Matrix projects every model vertex from the light position onto the plane.
    shadowMat[0][0] = dot - lightpos[0] * groundplane[0];
    shadowMat[1][0] = 0.0f - lightpos[0] * groundplane[1];
    shadowMat[2][0] = 0.0f - lightpos[0] * groundplane[2];
    shadowMat[3][0] = 0.0f - lightpos[0] * groundplane[3];

    shadowMat[0][1] = 0.0f - lightpos[1] * groundplane[0];
    shadowMat[1][1] = dot - lightpos[1] * groundplane[1];
    shadowMat[2][1] = 0.0f - lightpos[1] * groundplane[2];
    shadowMat[3][1] = 0.0f - lightpos[1] * groundplane[3];

    shadowMat[0][2] = 0.0f - lightpos[2] * groundplane[0];
    shadowMat[1][2] = 0.0f - lightpos[2] * groundplane[1];
    shadowMat[2][2] = dot - lightpos[2] * groundplane[2];
    shadowMat[3][2] = 0.0f - lightpos[2] * groundplane[3];

    shadowMat[0][3] = 0.0f - lightpos[3] * groundplane[0];
    shadowMat[1][3] = 0.0f - lightpos[3] * groundplane[1];
    shadowMat[2][3] = 0.0f - lightpos[3] * groundplane[2];
    shadowMat[3][3] = dot - lightpos[3] * groundplane[3];
}

static void SetMaterial(float r, float g, float b, float shininess, bool metallic)
{
    if (g_renderingShadow)
    {
        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float diffuse[] = { r, g, b, 1.0f };
    float ambient[] = { r * 0.18f, g * 0.18f, b * 0.18f, 1.0f };
    float specular[] = {
        metallic ? 0.9f : 0.15f,
        metallic ? 0.9f : 0.15f,
        metallic ? 0.9f : 0.15f,
        1.0f
    };

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shininess);
}

static void SetPlasticMaterial(float r, float g, float b, float shininess, float specStrength)
{
    if (g_renderingShadow)
    {
        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float diffuse[] = { r, g, b, 1.0f };
    float ambient[] = { r * 0.22f, g * 0.22f, b * 0.22f, 1.0f };
    float specular[] = { specStrength, specStrength, specStrength, 1.0f };

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shininess);
}

static void DrawBox(float sx, float sy, float sz)
{
    glPushMatrix();
    glScalef(sx, sy, sz);
    glutSolidCube(1.0f);
    glPopMatrix();
}

static void DrawBoxWithWeave(float sx, float sy, float sz)
{
    glPushMatrix();
    glScalef(sx, sy, sz);
    glutSolidCube(1.0f);

    glDisable(GL_LIGHTING);
    glColor3f(0.10f, 0.10f, 0.12f);
    glLineWidth(1.0f);

    const float eps = 0.0025f;
    const float z = 0.5f + eps;
    const int n = 14;

    glBegin(GL_LINES);
    for (int i = -n; i <= n; ++i)
    {
        float t = (float)i / (float)n;
        float x = t * 0.5f;
        glVertex3f(x, -0.5f, z);
        glVertex3f(x, 0.5f, z);
    }

    const int m = 18;
    for (int i = -m; i <= m; ++i)
    {
        float y0 = -0.5f + (float)i / (float)m;
        float y1 = y0 + 1.0f;
        glVertex3f(-0.5f, y0, z);
        glVertex3f(0.5f, y1, z);

        glVertex3f(-0.5f, y1, z);
        glVertex3f(0.5f, y0, z);
    }
    glEnd();

    glEnable(GL_LIGHTING);
    glPopMatrix();
}

static void DrawDisplayStage()
{
    glPushMatrix();
    glTranslatef(0.0f, STAGE_CENTER_Y, 0.0f);

    SetMaterial(1.0f, 1.0f, 1.0f, 120.0f, false);
    DrawBox(STAGE_WIDTH, STAGE_HEIGHT, STAGE_DEPTH);

    glPushMatrix();
    glTranslatef(0.0f, STAGE_HEIGHT * 0.5f + 0.04f, 0.0f);
    SetPlasticMaterial(1.0f, 1.0f, 1.0f, 90.0f, 0.20f);
    DrawBox(STAGE_WIDTH * 0.96f, 0.08f, STAGE_DEPTH * 0.92f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, -STAGE_HEIGHT * 0.28f, 0.0f);
    SetMaterial(1.0f, 1.0f, 1.0f, 90.0f, false);
    DrawBox(STAGE_WIDTH * 1.02f, STAGE_HEIGHT * 0.22f, STAGE_DEPTH * 1.02f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, -STAGE_HEIGHT * 0.5f - 0.03f, 0.0f);
    SetMaterial(1.0f, 1.0f, 1.0f, 70.0f, false);
    DrawBox(STAGE_WIDTH * 1.04f, 0.10f, STAGE_DEPTH * 1.04f);
    glPopMatrix();

    glPopMatrix();
}

static void WriteStageTopStencilMask()
{
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);

    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

    float halfW = STAGE_STENCIL_WIDTH * 0.5f;
    float halfD = STAGE_STENCIL_DEPTH * 0.5f;

    const float maskY = STAGE_TOP_Y + 0.002f;

    glBegin(GL_QUADS);
    glVertex3f(-halfW, maskY,  halfD);
    glVertex3f( halfW, maskY,  halfD);
    glVertex3f( halfW, maskY, -halfD);
    glVertex3f(-halfW, maskY, -halfD);
    glEnd();

    glPopAttrib();
}

static GLuint LoadTexture(const char* filename)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    wchar_t wideFilename[MAX_PATH] = { 0 };
    MultiByteToWideChar(CP_ACP, 0, filename, -1, wideFilename, MAX_PATH);

    wchar_t exePath[MAX_PATH] = { 0 };
    wchar_t exeDir[MAX_PATH] = { 0 };
    wchar_t projectPath[MAX_PATH] = { 0 };
    wchar_t fullProjectPath[MAX_PATH] = { 0 };
    wchar_t sourceDirPath[MAX_PATH] = { 0 };

    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wcsncpy(exeDir, exePath, MAX_PATH - 1);
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    swprintf(projectPath, MAX_PATH, L"%ls\\..\\..\\%ls", exeDir, wideFilename);
    GetFullPathNameW(projectPath, MAX_PATH, fullProjectPath, nullptr);
    swprintf(sourceDirPath, MAX_PATH, L"C:\\Users\\32100\\Desktop\\课件\\CG\\assignment\\%ls", wideFilename);

    unsigned char* data = stbi_load_w(wideFilename, &width, &height, &channels, 4);
    if (!data)
        data = stbi_load_w(fullProjectPath, &width, &height, &channels, 4);
    if (!data)
        data = stbi_load_w(sourceDirPath, &width, &height, &channels, 4);

    if (!data)
    {
        std::printf("Failed to load texture: %s\n", filename);
        return 0;
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return textureId;
}

static std::vector<Vec2> BuildRoundedRectContour(float w, float h, float r, int segPerCorner)
{
    std::vector<Vec2> pts;
    r = fminf(r, fminf(w, h) * 0.49f);

    float hw = w * 0.5f;
    float hh = h * 0.5f;

    auto addArc = [&](float cx, float cy, float a0, float a1)
        {
            for (int i = 0; i <= segPerCorner; ++i)
            {
                // t moves from 0 to 1, then interpolates between the two angles.
                float t = (float)i / (float)segPerCorner;
                float a = a0 + (a1 - a0) * t;
                // Circle equation: x = centerX + r*cos(a), y = centerY + r*sin(a).
                pts.push_back({ cx + r * cosf(a), cy + r * sinf(a) });
            }
        };

    addArc(hw - r, hh - r, 0.0f, 0.5f * 3.1415926f);
    addArc(-hw + r, hh - r, 0.5f * 3.1415926f, 3.1415926f);
    addArc(-hw + r, -hh + r, 3.1415926f, 1.5f * 3.1415926f);
    addArc(hw - r, -hh + r, 1.5f * 3.1415926f, 2.0f * 3.1415926f);

    if (!pts.empty()) pts.pop_back();
    return pts;
}

static void DrawTexturedRoundedRectFace(float w, float h, float r, float z, GLuint textureId)
{
    if (g_renderingShadow) return;
    if (textureId == 0) return;

    auto contour = BuildRoundedRectContour(w, h, r, 24);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor3f(1.0f, 1.0f, 1.0f);
    glNormal3f(0.0f, 0.0f, 1.0f);

    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0.5f, 0.5f);
    glVertex3f(0.0f, 0.0f, z);
    for (const auto& p : contour)
    {
        // Convert local face coordinates into texture coordinates around the center.
        float u = 0.5f + p.x / w;
        float v = 0.5f + p.y / h;
        glTexCoord2f(u, v);
        glVertex3f(p.x, p.y, z);
    }
    glTexCoord2f(0.5f + contour[0].x / w, 0.5f + contour[0].y / h);
    glVertex3f(contour[0].x, contour[0].y, z);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

static void DrawTexturedEllipseRing(float innerR, float outerR, float z, GLuint textureId)
{
    if (g_renderingShadow) return;
    if (textureId == 0) return;

    const int segments = 72;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor3f(1.0f, 1.0f, 1.0f);
    glNormal3f(0.0f, 0.0f, 1.0f);

    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= segments; ++i)
    {
        // Sample points around a circle; inner/outer radii make the textured ring.
        float a = 2.0f * 3.1415926f * (float)i / (float)segments;
        float c = cosf(a);
        float s = sinf(a);
        float ox = c * outerR;
        float oy = s * outerR;
        float ix = c * innerR;
        float iy = s * innerR;

        glTexCoord2f(0.5f + ox / (2.0f * outerR), 0.5f + oy / (2.0f * outerR));
        glVertex3f(ox, oy, z);
        glTexCoord2f(0.5f + ix / (2.0f * outerR), 0.5f + iy / (2.0f * outerR));
        glVertex3f(ix, iy, z);
    }
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

static void DrawBackgroundImage()
{
    if (backgroundTexture == 0) return;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, 1.0, 0.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 1.0f);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glPopMatrix();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glMatrixMode(GL_MODELVIEW);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

static void DrawFace(const std::vector<Vec2>& contour, float z, float normalZ)
{
    glNormal3f(0.0f, 0.0f, normalZ);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0.0f, 0.0f, z);
    for (const auto& p : contour) glVertex3f(p.x, p.y, z);
    glVertex3f(contour[0].x, contour[0].y, z);
    glEnd();
}

static void DrawLoft(const std::vector<Vec2>& a, float za, const std::vector<Vec2>& b, float zb)
{
    int n = (int)a.size();
    glBegin(GL_QUAD_STRIP);

    for (int i = 0; i <= n; ++i)
    {
        int idx = (i == n) ? 0 : i;
        int ip = (idx - 1 + n) % n;
        int in = (idx + 1) % n;

        Vec3 paPrev = { a[ip].x, a[ip].y, za };
        Vec3 paNext = { a[in].x, a[in].y, za };
        Vec3 pa = { a[idx].x, a[idx].y, za };
        Vec3 pb = { b[idx].x, b[idx].y, zb };

        // Tangent follows the contour, edge connects front/back; cross gives side normal.
        Vec3 tangent = Sub(paNext, paPrev);
        Vec3 edge = Sub(pb, pa);
        Vec3 nrm = Normalize(Cross(tangent, edge));

        glNormal3f(nrm.x, nrm.y, nrm.z);
        glVertex3f(a[idx].x, a[idx].y, za);
        glVertex3f(b[idx].x, b[idx].y, zb);
    }

    glEnd();
}

static void DrawBeveledRoundedCase(float w, float h, float t, float r,
    float bevelInset, float bevelH, int cornerSegs)
{
    // z0/z3 are back/front faces; z1/z2 create the bevel transition depth.
    float z0 = -t * 0.5f;
    float z3 = t * 0.5f;
    float z1 = z0 + bevelH;
    float z2 = z3 - bevelH;

    float wi = w - 2.0f * bevelInset;
    float hi = h - 2.0f * bevelInset;
    float ri = r - bevelInset * 0.85f;

    // Outer and inner contours are lofted together to form rounded beveled sides.
    auto outer = BuildRoundedRectContour(w, h, r, cornerSegs);
    auto inner = BuildRoundedRectContour(wi, hi, ri, cornerSegs);

    DrawLoft(outer, z0, inner, z1);
    DrawLoft(inner, z1, inner, z2);
    DrawLoft(inner, z2, outer, z3);

    DrawFace(outer, z3, +1.0f);
    DrawFace(outer, z0, -1.0f);
}

static void DrawBitmapText(void* font, const char* s, float x, float y, float z)
{
    glRasterPos3f(x, y, z);
    for (const char* p = s; *p; ++p)
        glutBitmapCharacter(font, *p);
}

static int BitmapTextWidth(void* font, const char* s)
{
    int w = 0;
    for (const char* p = s; *p; ++p)
        w += glutBitmapWidth(font, *p);
    return w;
}

static void DrawBitmapTextCentered(void* font, const char* s, float cx, float y, float z, float pixelToWorld)
{
    int px = BitmapTextWidth(font, s);
    float half = 0.5f * px * pixelToWorld;
    DrawBitmapText(font, s, cx - half, y, z);
}

static float StrokeTextWidth(void* font, const char* s)
{
    float w = 0.0f;
    for (const char* p = s; *p; ++p)
        w += (float)glutStrokeWidth(font, *p);
    return w;
}

static void DrawStrokeTextCentered(void* font, const char* s, float cx, float y, float z, float scale)
{
    if (g_renderingShadow) return;

    float half = 0.5f * StrokeTextWidth(font, s) * scale;

    glDisable(GL_LIGHTING);
    glColor3f(0.06f, 0.06f, 0.06f);
    glLineWidth(1.2f);

    glPushMatrix();
    glTranslatef(cx - half, y, z);
    glScalef(scale, scale, scale);
    for (const char* p = s; *p; ++p)
        glutStrokeCharacter(font, *p);
    glPopMatrix();

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

static void DrawScreenStrokeText(void* font, const char* s, float cx, float y, float z, float scale)
{
    if (g_renderingShadow) return;

    float half = 0.5f * StrokeTextWidth(font, s) * scale;

    glPushMatrix();
    glTranslatef(cx - half, y, z);
    glScalef(scale, scale, scale);
    for (const char* p = s; *p; ++p)
        glutStrokeCharacter(font, *p);
    glPopMatrix();
}

static void DrawWatchScreenText(float screenW, float screenH, float z)
{
    if (g_renderingShadow) return;

    char timeText[32];
    char glucoseText[40];
    std::snprintf(timeText, sizeof(timeText), "%02d:%02d", watchHour, watchMinute);
    std::snprintf(glucoseText, sizeof(glucoseText), "Glucose: %.0f mg/dL", glucoseValue);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.96f, 0.96f, 0.94f);
    glLineWidth(2.2f);

    DrawScreenStrokeText(GLUT_STROKE_ROMAN, timeText,
        0.0f, screenH * 0.18f, z, 0.06f);

    glLineWidth(1.5f);
    glColor3f(0.94f, 0.94f, 0.92f);
    DrawScreenStrokeText(GLUT_STROKE_ROMAN, "Blood Glucose",
        0.0f, screenH * -0.06f, z, 0.017f);

    if (glucoseValue > 140.0f)
        glColor3f(1.0f, 0.35f, 0.12f);
    else
        glColor3f(0.96f, 0.96f, 0.94f);

    DrawScreenStrokeText(GLUT_STROKE_ROMAN, glucoseText,
        0.0f, screenH * -0.28f, z, 0.018f);

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

static void DrawHeart2D(float cx, float cy, float size, float z)
{
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(cx, cy, z);

    for (int i = 0; i <= 64; ++i)
    {
        float t = (float)i / 64.0f * 2.0f * 3.1415926f;
        // Parametric heart curve used to draw the small health icon.
        float x = 16.0f * powf(sinf(t), 3.0f);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2.0f * t)
            - 2.0f * cosf(3.0f * t) - cosf(4.0f * t);

        x *= size * 0.030f;
        y *= size * 0.030f;

        glVertex3f(cx + x, cy + y, z);
    }

    glEnd();
}

static void DrawECGWave(float left, float right, float y, float amp, float z)
{
    glBegin(GL_LINE_STRIP);
    glVertex3f(left, y, z);
    glVertex3f(left + 2.0f, y, z);
    glVertex3f(left + 3.0f, y + amp, z);
    glVertex3f(left + 4.0f, y - amp * 1.5f, z);
    glVertex3f(left + 5.0f, y + amp * 2.0f, z);
    glVertex3f(left + 7.0f, y, z);
    glVertex3f(right, y, z);
    glEnd();
}

static void DrawProgressRing(float cx, float cy, float r, float z)
{
    glLineWidth(3.0f);
    glBegin(GL_LINE_STRIP);

    for (int i = 0; i <= 260; ++i)
    {
        // Partial circle equation; stopping at 260 degrees leaves a progress gap.
        float a = (float)i / 360.0f * 2.0f * 3.1415926f;
        glVertex3f(cx + cosf(a) * r, cy + sinf(a) * r, z);
    }

    glEnd();
    glLineWidth(1.0f);
}

static void DrawScreenUI(float screenW, float screenH)
{
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    const float z = 0.01f;
    const float pixelToWorld = screenW / 280.0f;

    glColor3f(0.10f, 0.10f, 0.12f);
    DrawBitmapTextCentered(GLUT_BITMAP_HELVETICA_18, "BLOOD GLUCOSE",
        0.0f, screenH * 0.40f, z, pixelToWorld);

    glColor3f(0.12f, 0.12f, 0.14f);
    DrawProgressRing(0.0f, screenH * 0.08f, screenW * 0.26f, z);

    glColor3f(0.08f, 0.08f, 0.10f);
    DrawBitmapTextCentered(GLUT_BITMAP_TIMES_ROMAN_24, "5.6",
        0.0f, screenH * 0.10f, z, pixelToWorld);

    glColor3f(0.18f, 0.18f, 0.20f);
    DrawBitmapTextCentered(GLUT_BITMAP_HELVETICA_12, "mmol/L",
        0.0f, screenH * -0.05f, z, pixelToWorld);

    glColor3f(0.10f, 0.10f, 0.12f);
    glLineWidth(1.3f);
    DrawECGWave(-screenW * 0.44f, -screenW * 0.22f, screenH * 0.22f, screenH * 0.06f, z);
    glLineWidth(1.0f);

    glColor3f(0.18f, 0.18f, 0.20f);
    DrawHeart2D(screenW * 0.36f, screenH * 0.22f, screenW * 0.12f, z);

    glColor3f(0.16f, 0.16f, 0.18f);
    glLineWidth(1.6f);

    float boxTop = -screenH * 0.18f;
    float boxBottom = -screenH * 0.45f;
    float boxLeft = -screenW * 0.46f;
    float boxMid = 0.0f;
    float boxRight = screenW * 0.46f;

    glBegin(GL_LINE_LOOP);
    glVertex3f(boxLeft, boxBottom, z);
    glVertex3f(boxMid, boxBottom, z);
    glVertex3f(boxMid, boxTop, z);
    glVertex3f(boxLeft, boxTop, z);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex3f(boxMid, boxBottom, z);
    glVertex3f(boxRight, boxBottom, z);
    glVertex3f(boxRight, boxTop, z);
    glVertex3f(boxMid, boxTop, z);
    glEnd();

    glLineWidth(1.0f);

    glColor3f(0.10f, 0.10f, 0.12f);
    DrawBitmapText(GLUT_BITMAP_HELVETICA_12, "Biochal Page", boxLeft + screenW * 0.05f, boxTop - screenH * 0.07f, z);
    DrawBitmapTextCentered(GLUT_BITMAP_TIMES_ROMAN_24, "5:6 OIL", boxLeft * 0.5f, boxBottom + screenH * 0.06f, z, pixelToWorld);

    DrawBitmapText(GLUT_BITMAP_HELVETICA_12, "Normal", boxMid + screenW * 0.05f, boxTop - screenH * 0.07f, z);
    DrawBitmapTextCentered(GLUT_BITMAP_TIMES_ROMAN_24, "5.6", boxRight * 0.5f, boxBottom + screenH * 0.10f, z, pixelToWorld);
    DrawBitmapTextCentered(GLUT_BITMAP_HELVETICA_12, "mol.", boxRight * 0.5f, boxBottom + screenH * 0.02f, z, pixelToWorld);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

static void DrawLug(float strapW, float y, float z)
{
    SetMaterial(0.62f, 0.64f, 0.66f, 130.0f, true);

    glPushMatrix();
    glTranslatef(0.0f, y, z);
    DrawBeveledRoundedCase(strapW + 2.0f, 3.0f, 3.0f, 1.2f, 0.35f, 0.35f, 12);
    glPopMatrix();
}

static void DrawCurvedStrap(bool top, float strapW, float strapT, float yAttach, float zAttach)
{
    SetPlasticMaterial(0.28f, 0.29f, 0.30f, 90.0f, 0.40f);

    const int segs = 12;
    const float arcR = 120.0f;
    const float thetaMaxDeg = 22.0f;
    const float sign = top ? 1.0f : -1.0f;

    for (int i = 0; i < segs; ++i)
    {
        // Each strap block represents a short arc segment.
        float t0 = (float)i / segs;
        float t1 = (float)(i + 1) / segs;
        float tm = 0.5f * (t0 + t1);

        float theta0 = thetaMaxDeg * 3.1415926f / 180.0f * t0;
        float theta1 = thetaMaxDeg * 3.1415926f / 180.0f * t1;
        float thetaM = thetaMaxDeg * 3.1415926f / 180.0f * tm;

        // Arc length = radius * angle; sin/cos place the segment on the curve.
        float segLen = arcR * (theta1 - theta0);
        float y = arcR * sinf(thetaM);
        float z = -arcR * (1.0f - cosf(thetaM));

        // Rotate the box so it follows the tangent of the strap curve.
        float rotX = -(thetaM * 180.0f / 3.1415926f);
        if (!top) rotX = -rotX;

        glPushMatrix();
        glTranslatef(0.0f, yAttach + sign * y, zAttach + z);
        glRotatef(rotX, 1.0f, 0.0f, 0.0f);
        DrawBox(strapW, segLen, strapT);
        glPopMatrix();
    }
}

static void DrawLoopStrap(float strapW, float strapT, float yLug, float zLug)
{
    SetPlasticMaterial(0.28f, 0.29f, 0.30f, 90.0f, 0.40f);

    const int segs = 160;
    const float Ry = 46.0f;
    const float Rz = 24.0f;
    const float gap = 5.5f;

    float s = yLug / Ry;
    if (s > 0.95f) s = 0.95f;
    if (s < -0.95f) s = -0.95f;

    // Reverse y = Ry*sin(t) to find where the loop should meet the lug.
    float tAttach = asinf(s);
    float zCenter = (zLug - gap) - Rz * cosf(tAttach);

    float dt = 2.0f * 3.1415926f / (float)segs;
    for (int i = 0; i < segs; ++i)
    {
        float t0 = dt * (float)i;
        float t1 = dt * (float)(i + 1);
        float tm = 0.5f * (t0 + t1);

        // Ellipse equation for the loop strap in the y-z plane.
        float y = Ry * sinf(tm);
        float z = zCenter + Rz * cosf(tm);

        // Derivative of the ellipse gives tangent direction and segment length.
        float dy = Ry * cosf(tm);
        float dz = -Rz * sinf(tm);

        float segLen = sqrtf(dy * dy + dz * dz) * dt;
        float rotX = atan2f(dz, dy) * 180.0f / 3.1415926f;

        glPushMatrix();
        glTranslatef(0.0f, y, z);
        glRotatef(rotX, 1.0f, 0.0f, 0.0f);
        DrawBox(strapW, segLen, strapT);
        glPopMatrix();
    }

}

static void DrawKnurledCrown(float bodyW)
{
    float knobR = 2.25f;
    float knobLen = 2.5f;
    float knobX = bodyW * 0.5f + knobLen * 0.45f;

    SetMaterial(0.70f, 0.72f, 0.75f, 160.0f, true);

    glPushMatrix();
    glTranslatef(knobX, 0.0f, 0.0f);
    glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
    glTranslatef(0.0f, 0.0f, -knobLen * 0.5f);

    gluCylinder(g_quad, knobR, knobR, knobLen, 36, 1);
    gluDisk(g_quad, 0.0, knobR, 36, 1);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, knobLen);
    gluDisk(g_quad, 0.0, knobR, 36, 1);
    glPopMatrix();

    const int ridges = 28;
    for (int i = 0; i < ridges; ++i)
    {
        float a = (float)i / (float)ridges * 360.0f;
        glPushMatrix();
        glRotatef(a, 0.0f, 0.0f, 1.0f);
        glTranslatef(knobR * 0.92f, 0.0f, knobLen * 0.5f);
        glScalef(0.35f, 0.18f, knobLen * 0.92f);
        glutSolidCube(1.0f);
        glPopMatrix();
    }

    glPopMatrix();
}

static void DrawInsulinDeliveryModule(float x, float y, float z)
{
    glPushMatrix();
    glTranslatef(x, y, z);

    SetPlasticMaterial(0.46f, 0.47f, 0.48f, 90.0f, 0.38f);
    DrawBeveledRoundedCase(29.0f, 24.0f, 5.8f, 5.8f, 0.42f, 0.42f, 20);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 3.25f);
    SetMaterial(0.72f, 0.74f, 0.76f, 130.0f, true);
    DrawBeveledRoundedCase(23.0f, 18.5f, 1.0f, 4.8f, 0.18f, 0.18f, 18);
    DrawTexturedRoundedRectFace(23.0f, 18.5f, 4.8f, 0.53f, grayPatchTexture);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 3.85f);
    glScalef(1.05f, 1.45f, 1.0f);
    SetMaterial(0.16f, 0.17f, 0.18f, 55.0f, false);
    gluCylinder(g_quad, 4.8f, 4.8f, 0.55f, 44, 1);
    gluDisk(g_quad, 0.0, 4.8f, 44, 1);
    DrawTexturedEllipseRing(2.6f, 4.8f, 0.57f, deepGrayPatchTexture);
    glTranslatef(0.0f, 0.0f, 0.56f);
    SetPlasticMaterial(0.61f, 0.82f, 0.92f, 55.0f, 0.50f);
    gluDisk(g_quad, 0.0, 2.6f, 44, 1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(14.75f, 0.0f, 0.8f);
    SetMaterial(0.12f, 0.12f, 0.13f, 35.0f, false);
    DrawBox(0.3f, 1.8f, 1.0f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-14.75f, 0.0f, 0.8f);
    SetMaterial(0.55f, 0.56f, 0.57f, 70.0f, false);
    DrawBox(0.3f, 4.0f, 2.0f);
    glPopMatrix();

    SetMaterial(0.78f, 0.55f, 0.24f, 120.0f, true);
    for (int i = -1; i <= 1; ++i)
    {
        glPushMatrix();
        glTranslatef(-14.95f, i * 1.15f, 0.8f);
        glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
        gluDisk(g_quad, 0.0, 0.36f, 28, 1);
        glPopMatrix();
    }

    glPopMatrix();
}

static void DrawExplodedNumber(int number, float x, float y, float z)
{
    (void)number;
    (void)x;
    (void)y;
    (void)z;
}

static void DrawMicroneedlePatchPart(float y)
{
    (void)y;
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glScalef(0.88f, 0.88f, 0.88f);
    SetPlasticMaterial(0.72f, 0.73f, 0.74f, 80.0f, 0.40f);
    DrawBeveledRoundedCase(29.0f, 24.0f, 1.6f, 5.8f, 0.22f, 0.22f, 20);
    DrawTexturedRoundedRectFace(29.0f, 24.0f, 5.8f, 0.82f, grayPatchTexture);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 1.05f);
    glScalef(0.85f, 1.25f, 1.0f);
    SetMaterial(0.34f, 0.35f, 0.36f, 55.0f, false);
    gluDisk(g_quad, 3.5f, 6.0f, 48, 1);
    DrawTexturedEllipseRing(3.5f, 6.0f, 0.01f, deepGrayPatchTexture);
    SetPlasticMaterial(0.62f, 0.82f, 0.90f, 45.0f, 0.45f);
    gluDisk(g_quad, 0.0f, 3.0f, 48, 1);
    glPopMatrix();

    DrawExplodedNumber(1, -18.0f, -2.0f, 2.2f);
}

static void DrawDrugReservoirPart(float y)
{
    (void)y;
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glScalef(0.88f, 0.88f, 0.88f);
    SetPlasticMaterial(0.58f, 0.82f, 0.90f, 55.0f, 0.55f);
    DrawBeveledRoundedCase(29.0f, 24.0f, 3.0f, 5.8f, 0.25f, 0.25f, 20);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 1.75f);
    SetPlasticMaterial(0.70f, 0.90f, 0.98f, 45.0f, 0.35f);
    DrawBeveledRoundedCase(21.0f, 15.5f, 0.7f, 4.2f, 0.12f, 0.12f, 16);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(14.65f, 0.0f, 1.1f);
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    SetMaterial(0.12f, 0.12f, 0.13f, 35.0f, false);
    DrawBox(4.0f, 0.45f, 1.2f);
    glPopMatrix();

    DrawExplodedNumber(2, -18.0f, -2.0f, 3.3f);
}

static void DrawSignalReceiverPart(float y)
{
    (void)y;
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glScalef(0.88f, 0.88f, 0.88f);
    SetPlasticMaterial(0.10f, 0.32f, 0.22f, 60.0f, 0.20f);
    DrawBeveledRoundedCase(29.0f, 24.0f, 1.3f, 5.8f, 0.16f, 0.16f, 20);

    SetMaterial(0.14f, 0.14f, 0.15f, 55.0f, false);
    for (int i = -1; i <= 1; ++i)
    {
        glPushMatrix();
        glTranslatef(i * 6.2f, 1.5f, 1.15f);
        DrawBox(4.3f, 3.5f, 0.65f);
        glPopMatrix();
    }

    if (!g_renderingShadow)
    {
        glDisable(GL_LIGHTING);
        glColor3f(0.82f, 0.66f, 0.32f);
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (int i = -3; i <= 3; ++i)
        {
            float yy = i * 2.2f;
            glVertex3f(-11.5f, yy, 1.55f);
            glVertex3f(11.5f, yy + (i % 2) * 1.0f, 1.55f);
        }
        glEnd();
        glEnable(GL_LIGHTING);
    }

    SetMaterial(0.78f, 0.55f, 0.24f, 100.0f, true);
    for (int i = 0; i < 6; ++i)
    {
        glPushMatrix();
        glTranslatef(-11.5f + i * 4.6f, -8.5f, 1.60f);
        gluDisk(g_quad, 0.0, 0.45f, 20, 1);
        glPopMatrix();
    }

    DrawExplodedNumber(3, -18.0f, -2.0f, 2.1f);
}

static void DrawBatteryPart(float y)
{
    (void)y;
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glScalef(0.88f, 0.88f, 0.88f);
    SetPlasticMaterial(0.90f, 0.88f, 0.82f, 50.0f, 0.25f);
    DrawBeveledRoundedCase(29.0f, 24.0f, 2.4f, 5.8f, 0.18f, 0.18f, 20);

    DrawStrokeTextCentered(GLUT_STROKE_ROMAN, "3.7V", 0.0f, 2.1f, 1.55f, 0.033f);
    DrawStrokeTextCentered(GLUT_STROKE_ROMAN, "200mAh", 0.0f, -2.2f, 1.55f, 0.027f);

    glPushMatrix();
    glTranslatef(-14.65f, 0.0f, 0.0f);
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    SetMaterial(0.68f, 0.70f, 0.72f, 80.0f, true);
    DrawBox(5.0f, 0.7f, 1.4f);
    glPopMatrix();

    DrawExplodedNumber(4, -18.0f, -2.0f, 2.4f);
}

static void DrawBaseHousingPart(float y)
{
    (void)y;
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    SetPlasticMaterial(0.48f, 0.49f, 0.50f, 90.0f, 0.35f);
    DrawBeveledRoundedCase(29.0f, 24.0f, 4.5f, 5.8f, 0.38f, 0.38f, 20);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 2.05f);
    SetPlasticMaterial(0.30f, 0.31f, 0.32f, 60.0f, 0.18f);
    DrawBeveledRoundedCase(22.5f, 17.5f, 0.9f, 4.4f, 0.18f, 0.18f, 16);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 2.72f);
    SetPlasticMaterial(0.03f, 0.03f, 0.035f, 80.0f, 0.20f);
    DrawBeveledRoundedCase(27.0f, 22.0f, 0.55f, 5.1f, 0.15f, 0.15f, 18);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 2.68f);
    SetPlasticMaterial(0.28f, 0.29f, 0.30f, 50.0f, 0.12f);
    DrawBeveledRoundedCase(21.0f, 16.0f, 0.45f, 4.0f, 0.08f, 0.08f, 14);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-14.8f, 0.0f, 0.5f);
    SetMaterial(0.55f, 0.56f, 0.57f, 70.0f, false);
    DrawBox(0.35f, 4.0f, 2.0f);
    glPopMatrix();

    SetMaterial(0.78f, 0.55f, 0.24f, 120.0f, true);
    for (int i = -1; i <= 1; ++i)
    {
        glPushMatrix();
        glTranslatef(-15.05f, i * 1.15f, 0.5f);
        glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
        gluDisk(g_quad, 0.0, 0.36f, 28, 1);
        glPopMatrix();
    }

    SetMaterial(0.04f, 0.04f, 0.045f, 35.0f, false);
    for (int side = -1; side <= 1; side += 2)
    {
        glPushMatrix();
        glTranslatef(0.0f, side * 11.65f, 0.0f);
        DrawBeveledRoundedCase(20.0f, 0.012f, 2.25f, 0.006f, 0.001f, 0.001f, 8);
        glPopMatrix();
    }

    DrawExplodedNumber(5, -18.0f, -2.0f, 5.0f);
}

static void DrawInjectionModuleExplodedView()
{
    glPushMatrix();
    glTranslatef(72.0f, 0.0f, 0.0f);
    glScalef(0.72f, 0.72f, 0.72f);

    float anim = g_explodedAnim;
    float y1Original = 44.0f;
    float y1Collapsed = 2.5f;
    float y1 = y1Original * (1.0f - anim) + y1Collapsed * anim;
    float y2 = 22.0f * (1.0f - anim);
    float y3 = 0.0f;
    float y4 = -22.0f * (1.0f - anim);
    float y5 = -44.0f * (1.0f - anim);

    glPushMatrix();
    glTranslatef(0.0f, y1, 0.0f);
    DrawMicroneedlePatchPart(y1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, y2, 0.0f);
    DrawDrugReservoirPart(y2);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, y3, 0.0f);
    DrawSignalReceiverPart(y3);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, y4, 0.0f);
    DrawBatteryPart(y4);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, y5, 0.0f);
    DrawBaseHousingPart(y5);
    glPopMatrix();

    glPopMatrix();
}

static void DrawSensorModule(float x, float y, float z)
{
    SetMaterial(0.62f, 0.64f, 0.66f, 150.0f, true);

    const float R = 9.0f;
    const float T = 4.2f;
    const float innerR = 5.8f;
    const float rimR = 7.2f;

    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
    glTranslatef(0.0f, 0.0f, -T * 0.5f);

    gluCylinder(g_quad, R, R, T, 48, 1);
    gluDisk(g_quad, 0.0, R, 48, 1);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, T);
    gluDisk(g_quad, 0.0, R, 48, 1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, T * 0.60f);
    SetMaterial(0.78f, 0.79f, 0.81f, 110.0f, true);
    gluCylinder(g_quad, rimR, rimR, 0.7f, 48, 1);
    glTranslatef(0.0f, 0.0f, 0.7f);
    gluDisk(g_quad, 0.0, rimR, 48, 1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, T * 0.62f);
    SetMaterial(0.52f, 0.53f, 0.55f, 90.0f, true);
    gluCylinder(g_quad, innerR, innerR, 1.1f, 48, 1);
    glTranslatef(0.0f, 0.0f, 1.1f);
    gluDisk(g_quad, 0.0, innerR, 48, 1);
    glPopMatrix();

    glPopMatrix();

    glPushMatrix();
    glTranslatef(x - 1.2f, y, z - 1.2f);
    SetMaterial(0.14f, 0.14f, 0.16f, 30.0f, false);
    DrawBeveledRoundedCase(8.0f, 10.0f, 3.0f, 2.4f, 0.25f, 0.25f, 14);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(x + 1.3f, y - 4.8f, z - 1.0f);
    SetMaterial(0.70f, 0.72f, 0.75f, 140.0f, true);
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
    glTranslatef(0.0f, 0.0f, -1.0f);
    gluCylinder(g_quad, 0.9f, 0.9f, 2.0f, 24, 1);
    gluDisk(g_quad, 0.0, 0.9f, 24, 1);
    glTranslatef(0.0f, 0.0f, 2.0f);
    gluDisk(g_quad, 0.0, 0.9f, 24, 1);
    glPopMatrix();
}

static void DrawWatchBackModules(float bodyT)
{
    const float backZ = -bodyT * 0.5f - 0.32f;

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, backZ);

    // Subtle recessed back panel so the two functional modules read as parts of the watch body.
    SetMaterial(0.54f, 0.55f, 0.55f, 95.0f, true);
    DrawBeveledRoundedCase(24.0f, 11.5f, 0.36f, 2.6f, 0.10f, 0.10f, 16);

    // Charging port: black rounded capsule with two brass contact pads.
    glPushMatrix();
    glTranslatef(-6.1f, 0.0f, -0.30f);
    SetPlasticMaterial(0.035f, 0.035f, 0.038f, 70.0f, 0.18f);
    DrawBeveledRoundedCase(8.8f, 4.8f, 0.42f, 2.3f, 0.11f, 0.09f, 18);

    SetMaterial(0.85f, 0.68f, 0.36f, 145.0f, true);
    for (int i = -1; i <= 1; i += 2)
    {
        glPushMatrix();
        glTranslatef(i * 1.85f, 0.0f, -0.24f);
        glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
        gluDisk(g_quad, 0.0f, 0.78f, 36, 1);
        glPopMatrix();
    }
    glPopMatrix();

    // Blood glucose sensor: black rounded square recess with a small central sensor stack.
    glPushMatrix();
    glTranslatef(6.1f, 0.0f, -0.30f);
    SetPlasticMaterial(0.025f, 0.025f, 0.028f, 80.0f, 0.22f);
    DrawBeveledRoundedCase(7.1f, 7.1f, 0.45f, 1.55f, 0.12f, 0.09f, 16);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -0.31f);
    SetPlasticMaterial(0.10f, 0.10f, 0.11f, 65.0f, 0.16f);
    DrawBeveledRoundedCase(3.0f, 3.9f, 0.28f, 0.55f, 0.06f, 0.05f, 10);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -0.50f);
    SetMaterial(0.52f, 0.54f, 0.56f, 110.0f, true);
    DrawBox(1.05f, 2.0f, 0.12f);
    SetMaterial(0.82f, 0.64f, 0.32f, 140.0f, true);
    glTranslatef(0.0f, 0.0f, -0.10f);
    DrawBox(0.86f, 0.50f, 0.10f);
    glTranslatef(0.0f, -0.78f, 0.0f);
    DrawBox(0.86f, 0.50f, 0.10f);
    glPopMatrix();

    glPopMatrix();
    glPopMatrix();
}

static void DrawWatch()
{
    const float bodyW = 36.0f;
    const float bodyH = 42.0f;
    const float bodyT = 8.5f;
    const float bodyForwardOffset = -bodyT * 0.7f;

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, bodyForwardOffset);
    SetMaterial(0.62f, 0.64f, 0.66f, 140.0f, true);
    DrawBeveledRoundedCase(bodyW, bodyH, bodyT, 7.2f, 0.45f, 0.45f, 20);

    const float bezelInset = 1.1f;
    SetMaterial(0.75f, 0.76f, 0.78f, 160.0f, true);
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, bodyT * 0.5f - 0.55f);
    DrawBeveledRoundedCase(bodyW - bezelInset * 2.0f, bodyH - bezelInset * 2.0f, 1.1f, 6.6f, 0.22f, 0.22f, 18);
    glPopMatrix();

    const float screenW = bodyW * 0.80f;
    const float screenH = bodyH * 0.76f;
    const float screenT = 0.55f;
    const float screenZ = bodyT * 0.5f + 0.08f;

    SetPlasticMaterial(0.22f, 0.23f, 0.24f, 70.0f, 0.24f);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, screenZ);
    DrawBeveledRoundedCase(screenW, screenH, screenT, 5.2f, 0.14f, 0.14f, 16);
    DrawWatchScreenText(screenW, screenH, screenT * 0.5f + 0.10f);

    glPopMatrix();

    DrawWatchBackModules(bodyT);

    DrawKnurledCrown(bodyW);
    glPopMatrix();

    const float strapW = bodyW * 0.72f;
    const float strapT = 3.0f;
    const float lugZ = -bodyT * 0.15f;
    const float lugForwardOffset = bodyT * 0.6f;

    DrawLug(strapW, bodyH * 0.5f + 0.8f, lugZ - lugForwardOffset);
    DrawLug(strapW, -bodyH * 0.5f - 0.8f, lugZ - lugForwardOffset);

    DrawLoopStrap(strapW, strapT, bodyH * 0.5f + 0.8f, lugZ);

    const float loopRy = 46.0f;
    const float loopRz = 24.0f;
    const float loopGap = 5.5f;
    float attachRatio = (bodyH * 0.5f + 0.8f) / loopRy;
    if (attachRatio > 0.95f) attachRatio = 0.95f;
    if (attachRatio < -0.95f) attachRatio = -0.95f;
    // Match the back module to the lower point of the elliptical strap loop.
    float attachAngle = asinf(attachRatio);
    float loopCenterZ = (lugZ - loopGap) - loopRz * cosf(attachAngle);
    DrawInsulinDeliveryModule(0.0f, 0.0f, loopCenterZ - loopRz + 1.0f);
}

static Vec3 BezierPoint(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
{
    float u = 1.0f - t;
    // Cubic Bezier blending weights; they always add up to 1.
    float b0 = u * u * u;
    float b1 = 3.0f * u * u * t;
    float b2 = 3.0f * u * t * t;
    float b3 = t * t * t;

    // Weighted sum of the four control points gives the signal point position.
    return {
        b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
        b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y,
        b0 * p0.z + b1 * p1.z + b2 * p2.z + b3 * p3.z
    };
}

// Stage 2: Bezier signal transmission
// Draw signal curve from watch screen to injection module
static void DrawSignalBezier()
{
    if (g_renderingShadow) return;
    if (!signalActive || demoStage < 1) return;

    Vec3 p0 = { -18.4f, 0.0f, -5.0f };
    Vec3 p1 = { -32.0f, 2.5f, -14.0f };
    Vec3 p2 = { -30.0f, 2.0f, -40.0f };
    Vec3 p3 = { -16.0f, 0.0f, -51.0f };

    // Four points define a smooth Bezier path from the watch to the injection module.
    GLfloat ctrlPoints[4][3] = {
        { p0.x, p0.y, p0.z },
        { p1.x, p1.y, p1.z },
        { p2.x, p2.y, p2.z },
        { p3.x, p3.y, p3.z }
    };

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.1f, 0.85f, 1.0f, 0.18f);
    glLineWidth(2.0f);

    // OpenGL evaluator draws the complete curve from the control points.
    glMap1f(GL_MAP1_VERTEX_3, 0.0f, 1.0f, 3, 4, &ctrlPoints[0][0]);
    glEnable(GL_MAP1_VERTEX_3);
    glMapGrid1f(60, 0.0f, 1.0f);
    glEvalMesh1(GL_LINE, 0, 60);
    glDisable(GL_MAP1_VERTEX_3);
    glDisable(GL_BLEND);

    // signalT is the moving point's progress along the same Bezier curve.
    Vec3 sp = BezierPoint(p0, p1, p2, p3, signalT);
    glPushMatrix();
    glTranslatef(sp.x, sp.y, sp.z);
    glColor3f(0.0f, 1.0f, 1.0f);
    glutSolidSphere(1.2f, 24, 12);
    glColor3f(0.4f, 1.0f, 1.0f);
    glutWireSphere(2.0f, 20, 10);
    glPopMatrix();

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

static void ResetInjectionParticles()
{
    for (int i = 0; i < INJECTION_PARTICLE_COUNT; ++i)
    {
        float col = (float)((i % 10) - 4.5f);
        float row = (float)(((i / 10) % 8) - 3.5f);
        // life is staggered so the particles do not all start at the same position.
        injectionParticles[i].life = (float)i / (float)INJECTION_PARTICLE_COUNT;
        injectionParticles[i].speed = 0.012f + 0.004f * (float)(i % 4);
        injectionParticles[i].offsetX = col * 2.0f;
        injectionParticles[i].offsetZ = row * 2.0f;
        injectionParticles[i].radius = 0.24f + 0.05f * (float)(i % 3);
        injectionParticles[i].active = true;
    }
    injectionParticlesVisible = true;
}

static void ClearInjectionParticles()
{
    for (int i = 0; i < INJECTION_PARTICLE_COUNT; ++i)
    {
        injectionParticles[i].life = 1.0f;
        injectionParticles[i].active = false;
    }

    injectionParticlesVisible = false;
}

static void UpdateInjectionParticles()
{
    if (!injectionActive && !injectionParticlesVisible) return;

    bool anyVisible = false;
    for (int i = 0; i < INJECTION_PARTICLE_COUNT; ++i)
    {
        if (!injectionParticles[i].active) continue;

        injectionParticles[i].life += injectionParticles[i].speed;
        if (injectionParticles[i].life > 1.0f)
        {
            if (injectionActive)
                injectionParticles[i].life -= 1.0f;
            else
            {
                injectionParticles[i].life = 1.0f;
                injectionParticles[i].active = false;
            }
        }

        if (injectionParticles[i].active)
            anyVisible = true;
    }

    injectionParticlesVisible = anyVisible || injectionActive;
}

// Stage 3: continuous drug/energy injection particles
static void DrawInjectionParticles()
{
    if (g_renderingShadow) return;
    if (!injectionParticlesVisible || demoStage < 3) return;

    const Vec3 emitter = { 0.0f, -1.0f, -51.0f };

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    for (int i = 0; i < INJECTION_PARTICLE_COUNT; ++i)
    {
        const InjectionParticle& p = injectionParticles[i];
        if (!p.active) continue;

        // life 0->1 becomes forward travel, while fade makes particles disappear.
        float travel = p.life * 13.0f;
        float fade = 1.0f - p.life;

        glPushMatrix();
        glTranslatef(
            emitter.x + p.offsetX * (1.0f + p.life * 0.08f),
            emitter.y + p.offsetZ,
            emitter.z + travel
        );
        glColor4f(0.18f, 0.88f, 1.0f, 0.22f + fade * 0.55f);
        glutSolidSphere(p.radius * (0.7f + fade * 0.8f), 12, 8);
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void SetupLights()
{
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    float globalAmbient[] = { 0.18f, 0.18f, 0.20f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);

    float dif0[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float spc0[] = { 1.0f, 1.0f, 1.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif0);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spc0);
    glLightfv(GL_LIGHT0, GL_POSITION, shadowLightPosition);
}

static void DrawSceneObjects()
{
    DrawWatch();
    DrawSignalBezier();
    DrawInjectionParticles();
    DrawInjectionModuleExplodedView();
}

static void DrawShadowSceneObjects()
{
    g_renderingShadow = true;

    DrawWatch();
    DrawInjectionModuleExplodedView();

    g_renderingShadow = false;
}

static int OverlayTextWidth(void* font, const char* text)
{
    int width = 0;
    for (const char* p = text; *p; ++p)
        width += glutBitmapWidth(font, *p);
    return width;
}

static void DrawOverlayTextLine(void* font, const char* text, float x, float y)
{
    glRasterPos2f(x, y);
    for (const char* p = text; *p; ++p)
        glutBitmapCharacter(font, *p);
}

static void DrawKeyboardHelpOverlay()
{
    int winW = glutGet(GLUT_WINDOW_WIDTH);
    int winH = glutGet(GLUT_WINDOW_HEIGHT);

    const char* lines[] = {
        "GlucoPatch Watch",
        "WASD: move view",
        "+/-: zoom",
        "E: explode / collapse",
        "Q: health demo",
        "ESC: exit"
    };
    const int lineCount = (int)(sizeof(lines) / sizeof(lines[0]));
    void* titleFont = GLUT_BITMAP_HELVETICA_18;
    void* bodyFont = GLUT_BITMAP_HELVETICA_12;

    int maxWidth = OverlayTextWidth(titleFont, lines[0]);
    for (int i = 1; i < lineCount; ++i)
    {
        int w = OverlayTextWidth(bodyFont, lines[i]);
        if (w > maxWidth) maxWidth = w;
    }

    const float margin = 18.0f;
    const float lineHeight = 17.0f;
    float x = (float)winW - margin - (float)maxWidth;
    float y = margin + (float)(lineCount - 1) * lineHeight;

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_LIGHTING_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glColor3f(1.0f, 1.0f, 1.0f);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, (GLdouble)winW, 0.0, (GLdouble)winH);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    DrawOverlayTextLine(titleFont, lines[0], x, y);
    for (int i = 1; i < lineCount; ++i)
        DrawOverlayTextLine(bodyFont, lines[i], x, y - (float)i * lineHeight);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glPopAttrib();
}

static void Display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    DrawBackgroundImage();

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    gluLookAt(0.0, 18.0, 225.0,
        0.0, 0.0, 0.0,
        0.0, 1.0, 0.0);

    shadowMatrix(stageShadowMatrix, stageTopPlane, shadowLightPosition);

    DrawDisplayStage();
    WriteStageTopStencilMask();

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_LIGHTING_BIT | GL_STENCIL_BUFFER_BIT);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_COLOR_MATERIAL);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilMask(0x00);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);

    glPushMatrix();
    glMultMatrixf((GLfloat*)stageShadowMatrix);
    glScalef(g_scale, g_scale, g_scale);
    glRotatef(g_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(g_rotY, 0.0f, 1.0f, 0.0f);
    DrawShadowSceneObjects();
    glPopMatrix();

    glDepthMask(GL_TRUE);
    glPopAttrib();

    glPushMatrix();
    glScalef(g_scale, g_scale, g_scale);
    glRotatef(g_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(g_rotY, 0.0f, 1.0f, 0.0f);
    DrawSceneObjects();
    glPopMatrix();

    DrawKeyboardHelpOverlay();

    glutSwapBuffers();
}

static void Reshape(int w, int h)
{
    if (h <= 0) h = 1;

    glViewport(0, 0, w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    // Perspective projection: 45 degree field of view, window aspect, near/far planes.
    gluPerspective(45.0, (double)w / (double)h, 0.1, 3000.0);

    glMatrixMode(GL_MODELVIEW);
}

static void Timer(int)
{
    const float speed = 0.045f;

    if (g_explodedAnim < g_explodedTarget)
    {
        // Move exploded-view animation gradually toward the selected target state.
        g_explodedAnim += speed;
        if (g_explodedAnim > g_explodedTarget)
            g_explodedAnim = g_explodedTarget;
    }
    else if (g_explodedAnim > g_explodedTarget)
    {
        g_explodedAnim -= speed;
        if (g_explodedAnim < g_explodedTarget)
            g_explodedAnim = g_explodedTarget;
    }

    if (g_explodedAnim < 0.0f) g_explodedAnim = 0.0f;
    if (g_explodedAnim > 1.0f) g_explodedAnim = 1.0f;

    glutPostRedisplay();
    glutTimerFunc(g_refreshMills, Timer, 0);
}

// Stage 1: glucose rising
static void updateDemo(int)
{
    if (demoStarted)
    {
        const float dt = 0.016f;
        const float riseDuration = 8.0f;
        const float signalStartT = 0.65f;

        demoTime += dt;

        if (demoStage == 1)
        {
            // Normalize elapsed time to a 0..1 progress value for the rising phase.
            float t = demoTime / riseDuration;
            if (t > 1.0f) t = 1.0f;
            if (t < 0.0f) t = 0.0f;

            // Linear interpolation: glucose rises from 90 to 180 as t goes 0..1.
            glucoseValue = 90.0f + (180.0f - 90.0f) * t;

            // Map the same progress to 30 minutes on the watch display.
            int elapsedMinutes = (int)(30.0f * t + 0.5f);
            watchHour = 8;
            watchMinute = elapsedMinutes;

            // Stage 2 preview: start sending the signal before glucose rising finishes.
            if (t >= signalStartT)
            {
                signalActive = true;
                // Remap the late part of stage 1 to Bezier progress for early signal motion.
                signalT = (t - signalStartT) / (1.0f - signalStartT) * 0.96f;
                if (signalT > 0.96f) signalT = 0.96f;
                if (signalT < 0.0f) signalT = 0.0f;
            }

            if (t >= 1.0f)
            {
                glucoseValue = 180.0f;
                watchMinute = 30;
                demoStage = 2;
                signalActive = true;
            }
        }
        else if (demoStage == 2)
        {
            // Continue moving the signal point until it reaches the injection module.
            signalT += 0.018f;
            if (signalT > 1.0f)
            {
                demoStage = 3;
                demoInjectionTime = 0.0f;
                injectionActive = true;
                signalT = 0.0f;
                ResetInjectionParticles();
            }
        }
        else if (demoStage == 3)
        {
            const float fallDuration = 10.0f;

            demoInjectionTime += dt;
            // Repeat signal movement while the injection particles are active.
            signalT += 0.018f;
            if (signalT > 1.0f)
                signalT = 0.0f;

            // Normalize injection time to interpolate glucose downward.
            float t = demoInjectionTime / fallDuration;
            if (t > 1.0f) t = 1.0f;
            if (t < 0.0f) t = 0.0f;

            // Linear interpolation: glucose falls from 180 to 100 during treatment.
            glucoseValue = 180.0f + (100.0f - 180.0f) * t;
            int elapsedMinutes = 30 + (int)(15.0f * t + 0.5f);
            watchHour = 8;
            watchMinute = elapsedMinutes;

            UpdateInjectionParticles();

            if (t >= 1.0f)
            {
                glucoseValue = 100.0f;
                watchMinute = 45;
                demoStage = 4;
                signalActive = false;
                signalT = 0.0f;
                injectionActive = false;
            }
        }
        else if (demoStage == 4)
        {
            glucoseValue = 100.0f;
            watchHour = 8;
            watchMinute = 45;
            signalActive = false;
            injectionActive = false;
            UpdateInjectionParticles();
        }

        glutPostRedisplay();
    }

    glutTimerFunc(16, updateDemo, 0);
}

static void Keyboard(unsigned char key, int, int)
{
    switch (key)
    {
    case 27:
        exit(0);
        break;

    case 'a':
    case 'A':
        g_rotY -= 3.0f;
        break;

    case 'd':
    case 'D':
        g_rotY += 3.0f;
        break;

    case 'w':
    case 'W':
        g_rotX -= 3.0f;
        break;

    case 's':
    case 'S':
        g_rotX += 3.0f;
        break;

    case '+':
    case '=':
        g_scale *= 1.06f;
        break;

    case '-':
    case '_':
        g_scale /= 1.06f;
        break;

    case 'e':
    case 'E':
        g_explodedCollapsed = !g_explodedCollapsed;
        g_explodedTarget = g_explodedCollapsed ? 1.0f : 0.0f;
        break;

    // Q key starts health demo
    case 'q':
    case 'Q':
        demoStarted = true;
        demoStage = 1;
        demoTime = 0.0f;
        demoInjectionTime = 0.0f;
        glucoseValue = 90.0f;
        watchHour = 8;
        watchMinute = 0;
        signalT = 0.0f;
        signalActive = false;
        injectionActive = false;
        ClearInjectionParticles();
        break;
    }

    glutPostRedisplay();
}

static void InitGL()
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);

    glClearColor(0.86f, 0.86f, 0.88f, 1.0f);
    glClearStencil(0);

    SetupLights();

    g_quad = gluNewQuadric();
    gluQuadricNormals(g_quad, GLU_SMOOTH);

    findPlane(stageTopPlane, stagePlaneVertices[1], stagePlaneVertices[2], stagePlaneVertices[3]);

    grayPatchTexture = LoadTexture("gray_patch_texture.jpg");
    deepGrayPatchTexture = LoadTexture("deepgray_patch_texture.jpg");
    backgroundTexture = LoadTexture("background.jpg");
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH | GLUT_STENCIL);
    glutInitWindowSize(960, 720);
    glutCreateWindow("GlucoPatch Watch");

    InitGL();

    glutDisplayFunc(Display);
    glutReshapeFunc(Reshape);
    glutKeyboardFunc(Keyboard);
    glutTimerFunc(g_refreshMills, Timer, 0);
    glutTimerFunc(0, updateDemo, 0);

    glutMainLoop();
    return 0;
}
