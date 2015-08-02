// (c) Petr Kalinin, https://github.com/petr-kalinin/radarmap, GNU AGPL
#include <iostream>
#include <sstream>
#include <cmath>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <proj_api.h>

using namespace std;
using namespace cv;

typedef Mat_<Vec4b> Image;
const Vec4b backgroundOuter(164,160,160,255);
const Vec4b lineColor(115,115,115,255);
const Vec4b backgroundInner(208,208,208,255);
const Vec4b boundaryColor(128,0,0,255);
const Vec4b transparent(0,0,0,0);
const Vec4b black(0,0,0,255);
const vector<Vec4b> palette{ // generated using make_palette
                        {0, 0, 95, 255},
                        {0, 0, 255, 255},
                        {0, 68, 136, 255},
                        {0, 102, 204, 255},
                        {0, 152, 0, 255},
                        {90, 194, 0, 255},
                        {95, 63, 63, 255},
                        {116, 0, 0, 255},
                        {127, 85, 255, 255},
                        {127, 170, 255, 255},
                        {128, 255, 255, 255},
                        {147, 255, 70, 255},
                        {177, 170, 156, 255},
                        {199, 0, 199, 255},
                        {255, 56, 1, 255},
                        {255, 85, 255, 255},
                        {255, 136, 62, 255},
                        {255, 170, 255, 255},
                        {255, 198, 162, 255}};
const Vec4b badPaletteColor{0, 68, 136, 255}; // this is the color that sometimes coincides with a color of a road
                                        // it is accepted as a color to be left on the map
                                        // but not as a replacement color 
const int colorEps = 2;
const int defaultLineDelta = 120;


struct point {
    double x,y;
    point(double _x, double _y): x(_x), y(_y) {}
};

ostream& operator<<(ostream& s, point p) {
    s << "(" << p.x << " " << p.y << ")";
    return s;
}

point transform(const projPJ& from, const projPJ& to, point xy) {
    pj_transform(from, to, 1, 1, &xy.x, &xy.y, NULL);
    return xy;
}

bool eqColor(const Vec4b& a, const Vec4b& b, const int eps=colorEps) {
    int diff = abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2]) + abs(a[3]-b[3]);
    return diff < eps;
}

bool boundaryPoint(const Image& im, int x, int y) {
    int cntOuter = 0, cntInner=0;
    for (int dy=-2; dy<=2; dy++) 
        for (int dx=-2; dx<=2; dx++) {
            int cx = x + dx;
            int cy = y + dy;
            if (cx>=0 && cx<im.cols && cy>=0 && cy<im.rows) {
                if (eqColor(im(cy,cx), backgroundOuter, 10)||(im(cy,cx)==transparent)) cntOuter++;
                if (eqColor(im(cy,cx), backgroundInner, 10)) cntInner++;
            } else cntOuter++;
        }
    return ((cntOuter > 5) && (cntInner > 5));
}

bool isPaletteColor(const Vec4b& col) {
    bool paletteColor = false;
    for(const auto& p: palette) 
        paletteColor = paletteColor || eqColor(p,col);
    return paletteColor;
}

Vec4b neibColor(const Image& im, int x, int y) {
    Vec4b res = transparent;
    int mindist = 10;
    for (int dx=-2; dx<2; dx++) 
        for (int dy=-2; dy<2; dy++) {
            int cx = x + dx;
            int cy = y + dy;
            if (cx>=0 && cx<im.cols && cy>=0 && cy<im.rows) {
                if (isPaletteColor(im(cy,cx)) && (im(cy,cx)!=badPaletteColor) && (abs(dx)+abs(dy)<mindist)) {
                    mindist = abs(dx)+abs(dy);
                    res = im(cy,cx);
                }
            }
        }
    return res;
}

bool isKeptBlack(const Image& im, int x, int y, const Image& stencil) {
    bool hasBlackNear = false;
    for (int dx=-2; dx<2; dx++) 
        for (int dy=-2; dy<2; dy++) {
            int cx = x + dx;
            int cy = y + dy;
            if (cx>=0 && cx<stencil.cols && cy>=0 && cy<stencil.rows) {
                hasBlackNear = hasBlackNear || eqColor(stencil(cy, cx), black, 10);
            }
        }
    
    return eqColor(im(y,x), black) && (!hasBlackNear);
}

bool isBadRoadColor(const Image& im, int x, int y, const Image& stencil) {
    return eqColor(im(y,x), badPaletteColor) && eqColor(stencil(y,x), badPaletteColor, 50);
}

Vec4b replacementColor(const Image& im, int x, int y, const Image& stencil) {
    if (boundaryPoint(im, x, y)) 
        return boundaryColor;

    if (((!isPaletteColor(im(y,x))) && (!isKeptBlack(im, x, y, stencil))) || (isBadRoadColor(im, x, y, stencil)))
        return neibColor(im, x, y);
    
    return im(y,x);
}

Image transformProjection(const Image& source, point earthCenterDeg, point sourceCenter, double sourcePixelPerRad, int targetHeight, const Image& stencil) {
    cout << "sourcePixelPerRad = " << sourcePixelPerRad << endl;
    double sourcePixelPerDeg = sourcePixelPerRad / 180 * M_PI;
    point earthCenterRad{
        earthCenterDeg.x / 180*M_PI, 
        earthCenterDeg.y / 180*M_PI
    };

    
    projPJ earthProj = pj_init_plus("+proj=latlong");
    if (!earthProj) {
        cout << "Can't create earthProj!" << endl;
        exit(1);
    }
    stringstream sourceCode;
    sourceCode.precision(15);
    sourceCode << "+proj=aeqd +R=1 +x_0=0 +y_0=0 +lon_0=" << earthCenterDeg.x << " +lat_0=" << earthCenterDeg.y;
    projPJ sourceProj = pj_init_plus(sourceCode.str().c_str());
    if (!sourceProj) {
        cout << "Can't create sourceProj!" << endl;
        exit(1);
    }
    projPJ targetProj = pj_init_plus("+init=epsg:3857");
    if (!targetProj) {
        cout << "Can't create targetProj!" << endl;
        exit(1);
    }

    cout << "Center@source: " << transform(earthProj, sourceProj, earthCenterRad) << endl;
    cout << "Center@target: " << transform(earthProj, targetProj, earthCenterRad) << endl;

    point earthRadiusRad{
        source.cols/2.0 / sourcePixelPerRad / cos(earthCenterRad.y), 
        source.rows/2.0 / sourcePixelPerRad
    };
    point earthTopLeftRad{
        earthCenterRad.x - earthRadiusRad.x,
        earthCenterRad.y - earthRadiusRad.y
    };
    point earthBotRightRad{ 
        earthCenterRad.x + earthRadiusRad.x, 
        earthCenterRad.y + earthRadiusRad.y
    };

    point targetTopLeft = transform(earthProj, targetProj, earthTopLeftRad);
    point targetBotRight = transform(earthProj, targetProj, earthBotRightRad);
    int targetWidth = int(targetHeight / (targetBotRight.y - targetTopLeft.y) * (targetBotRight.x - targetTopLeft.x));

    cout << "TargetTopLeft: " << targetTopLeft << endl;
    cout << "TargetBotRight: " << targetBotRight << endl;
    
    cout << "Earth radius: " << earthRadiusRad << endl;
    cout << "EarthTL: " << earthTopLeftRad << endl;
    cout << "EarthBR: " << earthBotRightRad << endl;

    cout << "SourceTopLeft: " << transform(targetProj, sourceProj, targetTopLeft) << endl;
    cout << "SourceBotRight: " << transform(targetProj, sourceProj, targetBotRight) << endl;

    cout << "target size: " << targetWidth << " " << targetHeight << endl;

    Image target(targetHeight, targetWidth, transparent);
    for (int targetYpx=0; targetYpx<target.rows; targetYpx++)
        for (int targetXpx=0; targetXpx<target.cols; targetXpx++) {
            point targetXY {
                targetTopLeft.x + ((targetBotRight.x-targetTopLeft.x) * targetXpx) / targetWidth,
                targetTopLeft.y + ((targetBotRight.y-targetTopLeft.y) * (targetHeight-targetYpx)) / targetHeight
            };
            point sourceXY = transform(targetProj, sourceProj, targetXY);
            int sourceXpx = int(sourceXY.x * sourcePixelPerRad + sourceCenter.x + 0.5);
            int sourceYpx = int(-sourceXY.y * sourcePixelPerRad + sourceCenter.y + 0.5);
            if (sourceXpx>=0 && sourceXpx<source.cols && sourceYpx>=0 && sourceYpx<source.rows) {
                target(targetYpx, targetXpx) = source(sourceYpx, sourceXpx);
            }
        }
    Image targetWoBackground = target.clone();
    std::cout << "Removing background" << std::endl;
    for (int targetYpx=0; targetYpx<target.rows; targetYpx++)
        for (int targetXpx=0; targetXpx<target.cols; targetXpx++) {
            targetWoBackground(targetYpx, targetXpx) = replacementColor(target, targetXpx, targetYpx, stencil);
        }
    
/*
    point targetCenter = transform(sourceProj, targetProj, point{0,0});
    int targetXpx = int((targetCenter.x - targetTopLeft.x)/(targetBotRight.x-targetTopLeft.x)*targetWidth + 0.5);
    int targetYpx = int((targetBotRight.y-targetCenter.y)/(targetBotRight.y-targetTopLeft.y)*targetHeight + 0.5);
    cout << "targetCenter: " << targetXpx << " " << targetYpx << endl;
    targetWoBackground(targetYpx, targetXpx) = Vec4b(255,0,255,255);
*/        
    cout << "Corner-coordinates of result: " << targetTopLeft << " " << targetBotRight << endl;

    return targetWoBackground;
}

std::vector<int> makeCumulative(const std::vector<int>& v) {
    static const int avgStep = 50;

    std::vector<int> res(v.size(), 0);
    for (int i=0; i<v.size(); i++)
        for (int j=-avgStep; j<=avgStep; j++) { 
            int pos = i+j;
            if (pos<0) pos=0;
            if (pos>=v.size()) pos=v.size()-1;
            res[i]+=v[pos];
        }
    return res;
}

void detectCenter(const Image& im, point& detectedCenter, float& detectedScaling) {
    std::vector<int> cntLineX(im.cols, 0);
    std::vector<int> cntLineY(im.rows, 0);
    std::vector<int> cntBgX(im.cols, 0);
    std::vector<int> cntBgY(im.rows, 0);
    for (int y=0; y<im.rows; y++)
        for (int x=0; x<im.cols; x++) {
                if (eqColor(im(y, x), backgroundOuter, 10)) { 
                    cntBgX[x]++;
                    cntBgY[y]++;
                }
                if (eqColor(im(y, x), lineColor, 10)) {
                    cntLineX[x]++;
                    cntLineY[y]++;
                }
        }
    std::vector<int> cumBgX = makeCumulative(cntBgX);
    std::vector<int> cumBgY = makeCumulative(cntBgY);
    
    int maxLineX = *max_element(cntLineX.begin(), cntLineX.end());
    int maxLineY = *max_element(cntLineY.begin(), cntLineY.end());
    cout << "maxLineX=" << maxLineX << " of " << im.rows << endl;
    cout << "maxLineY=" << maxLineY << " of " << im.cols << endl;
    std::map<int, int> deltas;
    
    point result{0,0};
    int prev = -1;
    for(int x=0; x<cntLineX.size(); x++)
        if (cntLineX[x] > maxLineX *7/10) {
            cout << "X-candidate: " << x << " " << double(cntLineX[x]) / maxLineX << " " << cumBgX[x] << endl;
            if (cumBgX[x] < cumBgX[result.x])
                result.x = x;
            if (prev != -1)
                deltas[x - prev] ++;
            prev = x;
        }
    prev = -1;
    for(int y=0; y<cntLineY.size(); y++)
        if (cntLineY[y] > maxLineY *7/10) {
            cout << "Y-candidate: " << y << " " << double(cntLineY[y]) / maxLineY << " " << cumBgY[y] << endl;
            if (cumBgY[y] < cumBgY[result.y])
                result.y = y;
            if (prev != -1)
                deltas[y - prev] ++;
            prev = y;
        }
    pair<int, int> maxDelta = *deltas.begin();
    for (auto v: deltas)
        if (v.second > maxDelta.second)
            maxDelta = v;
    cout << "Deltas: ";
    for (auto v: deltas)
        cout << v.first << " " << v.second << endl;
    detectedCenter = result;
    detectedScaling = (float)maxDelta.first / defaultLineDelta;
    cout << "Detected center @ " << detectedCenter << endl;
    cout << "Detected scaling " << detectedScaling << endl;
}

int main(int argc, char* argv[]) {
    cout.precision(10);
    /*
    #earthCenterDeg = (43.97838, 56.293779) #lat, lon
    #sourceName = 'UVKNizhny-crop.png'
    #targetName = 'UVKNizhny-merc.png'
    #sourceCenter = (588, 500) # x,y

    #earthCenterDeg = (41.016018, 57.809240) #lon, lat
    #sourceName = 'UVKKostroma-crop.png'
    #targetName = 'UVKKostroma-merc.png'
    #sourceCenter = (583, 493) # x,y

    point earthCenterDeg {37.549006,55.648246};
    std::string sourceName = "UVKProfsoyuz-crop.png";
    std::string targetName = "UVKProfsoyuz-merc.png";
    point sourceCenter {576, 459};
    */
    point earthCenterDeg {stof(argv[1]), stof(argv[2])};
    std::string sourceName(argv[3]);
    std::string targetName(argv[4]);
    std::string stencilName(argv[5]);

    double defaultSourcePixelPerRad = 12750;
    int targetHeight = 1000;
    
    // Setup a rectangle to define your region of interest
    Rect cropArea(185, 54, 1365-185, 1014-54);
    //------------------------

    Image source = imread(sourceName, -1);
    source = source(cropArea);
    
    Image stencil = imread(stencilName, -1);
    
    point sourceCenter{0,0};
    float sourceScaling = 1;
    detectCenter(source, sourceCenter, sourceScaling);
    
    Image result = transformProjection(source, earthCenterDeg, sourceCenter, defaultSourcePixelPerRad * sourceScaling, targetHeight, stencil);

    vector<int> compression_params;
    compression_params.push_back(CV_IMWRITE_PNG_COMPRESSION);
    compression_params.push_back(9);
    imwrite(targetName, result, compression_params);
    
    cout << "Wrote to " << targetName << endl;
    
    return 0;
}