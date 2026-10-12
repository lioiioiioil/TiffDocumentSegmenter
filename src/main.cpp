#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <opencv2/opencv.hpp>

// Чтение Tiff и Tif
class TifReader {
    std::ifstream file;
    bool little_endian = true;
    uint32_t width = 0, height = 0;
    uint16_t bits = 8, spp = 1, compression = 1, photo = 1;
    uint32_t rows_per_strip = 0;
    std::vector<uint32_t> strip_offsets, strip_byte_counts;

    uint16_t read_u16() {
        uint8_t b[2]; file.read((char*)b, 2);
        return little_endian ? (b[0] | (b[1] << 8)) : ((b[0] << 8) | b[1]);
    }
    uint32_t read_u32() {
        uint8_t b[4]; file.read((char*)b, 4);
        return little_endian ? (b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24)) 
                             : ((b[0]<<24) | (b[1]<<16) | (b[2]<<8) | b[3]);
    }
    uint32_t short_value(uint16_t type, uint32_t val) const {
        if (type == 3) return little_endian ? (val & 0xFFFF) : (val >> 16);
        if (type == 1) return little_endian ? (val & 0xFF) : (val >> 24);
        return val;
    }
    std::vector<uint32_t> read_values(uint16_t type, uint32_t count, uint32_t val) {
        std::vector<uint32_t> res;
        if (count == 1) return {short_value(type, val)};
        if (type == 3 && count == 2) {
            if (little_endian) return {val & 0xFFFF, (val >> 16) & 0xFFFF};
            return {(val >> 16) & 0xFFFF, val & 0xFFFF};
        }
        if (type == 1 && count <= 4) {
            for (uint32_t i = 0; i < count; ++i) {
                int shift = little_endian ? (8 * i) : (24 - 8 * i);
                res.push_back((val >> shift) & 0xFF);
            }
            return res;
        }
        std::streampos pos = file.tellg();
        file.seekg(val);
        for (uint32_t i = 0; i < count; ++i)
            res.push_back(type == 3 ? read_u16() : read_u32());
        file.seekg(pos);
        return res;
    }
    void read_tags(uint32_t offset) {
        file.seekg(offset);
        uint16_t n = read_u16();
        for (uint16_t i = 0; i < n; ++i) {
            uint16_t tag = read_u16(), type = read_u16();
            uint32_t count = read_u32(), val = read_u32();
            switch (tag) {
                case 256: width = read_values(type, count, val)[0]; break;
                case 257: height = read_values(type, count, val)[0]; break;
                case 258: bits = read_values(type, count, val)[0]; break;
                case 259: compression = read_values(type, count, val)[0]; break;
                case 262: photo = read_values(type, count, val)[0]; break;
                case 273: strip_offsets = read_values(type, count, val); break;
                case 277: spp = read_values(type, count, val)[0]; break;
                case 278: rows_per_strip = read_values(type, count, val)[0]; break;
                case 279: strip_byte_counts = read_values(type, count, val); break;
            }
        }
    }
public:
    bool load_gray(const std::string& path, std::vector<uint8_t>& out_gray, uint32_t& W, uint32_t& H) {
        file.open(path, std::ios::binary);
        if (!file.is_open()) return false;
        char magic[2]; file.read(magic, 2);
        little_endian = (magic[0] == 'I' && magic[1] == 'I');
        if (magic[0] != 'I' && magic[0] != 'M') return false;
        if (read_u16() != 42) return false;

        read_tags(read_u32());
        if (rows_per_strip == 0) rows_per_strip = height;
        if (compression != 1 || width == 0 || height == 0) return false;
        if (bits != 8) return false;

        W = width; H = height;
        bool isRGB = (photo == 2 && spp == 3);
        size_t logical_stride = W * (isRGB ? 3 : 1);
        size_t physical_stride = logical_stride;

        out_gray.resize(W * H);
        std::vector<uint8_t> lineBuf(physical_stride);
        size_t stripIdx = 0;
        for (uint32_t r = 0; r < H; ) {
            if (stripIdx >= strip_offsets.size()) break;
            uint32_t rowsInStrip = std::min(rows_per_strip, H - r);
            file.seekg(strip_offsets[stripIdx]);
            uint32_t expectedBytes = rowsInStrip * physical_stride;
            uint32_t actualBytes = (stripIdx < strip_byte_counts.size()) ? strip_byte_counts[stripIdx] : expectedBytes;

            for (uint32_t row = 0; row < rowsInStrip; ++row) {
                file.read((char*)lineBuf.data(), physical_stride);
                size_t dstOff = (r + row) * W;
                if (isRGB) {
                    for (uint32_t c = 0; c < W; ++c) {
                        int rv = lineBuf[c*3], gv = lineBuf[c*3+1], bv = lineBuf[c*3+2];
                        int g = (299 * rv + 587 * gv + 114 * bv + 50) / 1000;
                        out_gray[dstOff + c] = static_cast<uint8_t>(std::max(0, std::min(255, g)));
                    }
                } else {
                    std::memcpy(&out_gray[dstOff], lineBuf.data(), W);
                }
            }
            if (actualBytes > expectedBytes) file.seekg(actualBytes - expectedBytes, std::ios::cur);
            r += rowsInStrip; ++stripIdx;
        }
        if (photo == 0) for (auto& p : out_gray) p = 255 - p;
        return true;
    }
};

void histogram(const uint8_t* img, int W, int H, unsigned long hist[256]) {
    std::memset(hist, 0, 256 * sizeof(unsigned long));
    for (int i = 0; i < W * H; ++i) hist[img[i]]++;
}

int otsu(const unsigned long hist[256], int totalPixels) {
    int bestT = 128; double maxSigma = 0.0;
    double sumAll = 0;
    for (int i = 0; i < 256; ++i) sumAll += i * hist[i];
    double sumB = 0; long wB = 0;
    for (int t = 0; t < 255; ++t) {
        wB += hist[t]; if (wB == 0) continue;
        long wF = totalPixels - wB; if (wF == 0) break;
        sumB += t * hist[t];
        double mB = sumB / wB, mF = (sumAll - sumB) / wF;
        double sigma = wB * wF * (mB - mF) * (mB - mF);
        if (sigma > maxSigma) { maxSigma = sigma; bestT = t; }
    }
    if (bestT == 0 || bestT == 255) bestT = static_cast<int>(sumAll / totalPixels * 0.6);
    return bestT;
}

void sobel(const uint8_t* in, int W, int H, std::vector<uint8_t>& outMag) {
    outMag.assign(W * H, 0);
    int kx[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    int ky[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            int gx = 0, gy = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int val = in[(y + dy) * W + (x + dx)];
                    int idx = (dy + 1) * 3 + (dx + 1);
                    gx += val * kx[idx]; gy += val * ky[idx];
                }
            int mag = static_cast<int>(std::sqrt(gx * gx + gy * gy));
            outMag[y * W + x] = static_cast<uint8_t>(mag > 255 ? 255 : mag);
        }
    }
}

struct BlockInfo {
    double mean, variance, entropy, histEnergy;
    double glcmEnergy, glcmContrast;
    double avgGradient, edgeDensity;
    double backgroundRatio;
    double inkRatio;
    double whiteRatio;
    double darkRatio;
    double midtoneRatio;
};

BlockInfo block_info(const uint8_t* img, const uint8_t* grad, const uint8_t* mask, int bx, int by, int bw, int bh, int W) {
    BlockInfo f; 
    unsigned long hist[256] = {0}; 
    unsigned long glcm[256][256] = {{0}};
    int backgroundCount = 0, inkCount = 0, whiteCount = 0, darkCount = 0, midtoneCount = 0;
    int N = bw * bh;
    double gradSum = 0; int edgeCount = 0;
    long totalPairs = 0;

    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            int idx = (by + y) * W + (bx + x);
            uint8_t p = img[idx];
            hist[p]++;
            if (mask[idx] == 255) backgroundCount++;
            else inkCount++;
            if (p > 225) whiteCount++;
            if (p < 90) darkCount++;
            if (p >= 70 && p <= 220) midtoneCount++;
            int g = grad[idx];
            gradSum += g;
            if (g > 30) edgeCount++;
        }
    }
    f.backgroundRatio = (double)backgroundCount / N;
    f.inkRatio = (double)inkCount / N;
    f.whiteRatio = (double)whiteCount / N;
    f.darkRatio = (double)darkCount / N;
    f.midtoneRatio = (double)midtoneCount / N;

    double sum = 0;
    for (int i = 0; i < 256; ++i) sum += i * hist[i];
    f.mean = sum / N;
    double varSum = 0;
    for (int i = 0; i < 256; ++i) varSum += hist[i] * (i - f.mean) * (i - f.mean);
    f.variance = varSum / N;

    f.histEnergy = 0.0; f.entropy = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (hist[i] == 0) continue;
        double p = (double)hist[i] / N;
        f.histEnergy += p * p;
        f.entropy -= p * std::log2(p);
    }

    for (int y = 0; y < bh - 1; ++y) {
        for (int x = 0; x < bw; ++x) {
            int r = img[(by + y) * W + (bx + x)];
            int c = img[(by + y + 1) * W + (bx + x)];
            glcm[r][c]++; glcm[c][r]++; totalPairs += 2;
        }
    }
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw - 1; ++x) {
            int r = img[(by + y) * W + (bx + x)];
            int c = img[(by + y) * W + (bx + x + 1)];
            glcm[r][c]++; glcm[c][r]++; totalPairs += 2;
        }
    }
    f.glcmEnergy = 0.0; f.glcmContrast = 0.0;
    if (totalPairs > 0) {
        double norm = 1.0 / totalPairs;
        for (int i = 0; i < 256; ++i)
            for (int j = 0; j < 256; ++j) {
                if (glcm[i][j] == 0) continue;
                double p = glcm[i][j] * norm;
                f.glcmEnergy += p * p;
                f.glcmContrast += p * (i - j) * (i - j);
            }
    }

    f.avgGradient = gradSum / N;
    f.edgeDensity = (double)edgeCount / N;
    return f;
}

enum BlockClass { BG, TEXT, PHOTO, UNKNOWN };

BlockClass get_class(const BlockInfo& f) {
    if ((f.backgroundRatio > 0.985 && f.variance < 90.0 && f.entropy < 1.8) ||
        (f.inkRatio < 0.015 && f.avgGradient < 7.0) ||
        (f.variance < 35.0 && f.entropy < 1.2))
        return BG;

    int textScore = 0;
    textScore += (f.backgroundRatio > 0.58);
    textScore += (f.inkRatio > 0.015 && f.inkRatio < 0.38);
    textScore += (f.edgeDensity > 0.030 || f.avgGradient > 10.0);
    textScore += (f.histEnergy > 0.033);
    textScore += (f.entropy < 5.35);
    textScore += (f.midtoneRatio < 0.48);

    int photoScore = 0;
    photoScore += (f.backgroundRatio < 0.62);
    photoScore += (f.inkRatio > 0.30);
    photoScore += (f.midtoneRatio > 0.25);
    photoScore += (f.variance > 260.0);
    photoScore += (f.entropy > 3.30);
    photoScore += (f.histEnergy < 0.085);
    photoScore += (f.glcmContrast > 320.0);

    if (photoScore >= 3 && !(textScore >= 5 && f.backgroundRatio > 0.68))
        return PHOTO;
    if (textScore >= 3)
        return TEXT;

    if (f.backgroundRatio > 0.92 && f.variance < 140.0)
        return BG;
    return (photoScore > textScore) ? PHOTO : TEXT;
}

struct Rect { int x, y, w, h; BlockClass cls; };

void smooth(std::vector<BlockClass>& grid, int cols, int rows) {
    std::vector<BlockClass> tmp = grid;
    int dx[8] = {-1,0,1,-1,1,-1,0,1};
    int dy[8] = {-1,-1,-1,0,0,1,1,1};
    for (int r = 1; r < rows-1; ++r) {
        for (int c = 1; c < cols-1; ++c) {
            if (grid[r*cols+c] == BG) continue;
            int cntText = 0, cntPhoto = 0;
            for (int k = 0; k < 8; ++k) {
                int nr = r + dy[k], nc = c + dx[k];
                if (grid[nr*cols+nc] == TEXT) cntText++;
                else if (grid[nr*cols+nc] == PHOTO) cntPhoto++;
            }
            if (cntText > cntPhoto) tmp[r*cols+c] = TEXT;
            else if (cntPhoto > cntText) tmp[r*cols+c] = PHOTO;
        }
    }
    grid = tmp;
}

void clear_photo_noise(std::vector<BlockClass>& grid, int cols, int rows) {
    std::vector<bool> visited(rows * cols, false);
    int dx[4] = {1, -1, 0, 0};
    int dy[4] = {0, 0, 1, -1};

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (visited[idx] || grid[idx] != PHOTO) continue;

            std::vector<std::pair<int,int>> component;
            component.push_back({r, c});
            visited[idx] = true;
            int head = 0;
            int surroundingText = 0;
            int surroundingBoundary = 0;

            while (head < component.size()) {
                auto [cr, cc] = component[head++];
                for (int k = 0; k < 4; ++k) {
                    int nr = cr + dy[k], nc = cc + dx[k];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    int nidx = nr * cols + nc;
                    if (grid[nidx] == PHOTO && !visited[nidx]) {
                        visited[nidx] = true;
                        component.push_back({nr, nc});
                    } else if (grid[nidx] != PHOTO) {
                        surroundingBoundary++;
                        if (grid[nidx] == TEXT) surroundingText++;
                    }
                }
            }

            if (!component.empty()) {
                bool insideText = (component.size() <= 24 && surroundingBoundary > 0 && surroundingText >= surroundingBoundary * 0.50)
                    || (component.size() <= 18 && surroundingText >= 5);
                if (insideText) {
                    for (auto& p : component) grid[p.first * cols + p.second] = TEXT;
                }
            }
        }
    }
}

bool plain_bg(const BlockInfo& f) {
    return (f.backgroundRatio > 0.992 && f.variance < 55.0 && f.avgGradient < 5.0) ||
           (f.inkRatio < 0.010 && f.variance < 80.0);
}

bool looks_like_text(const BlockInfo& f) {
    bool regularText = f.backgroundRatio > 0.66 &&
                       f.inkRatio > 0.012 && f.inkRatio < 0.34 &&
                       (f.edgeDensity > 0.025 || f.avgGradient > 9.0) &&
                       f.histEnergy > 0.030 &&
                       f.entropy < 5.35;
    bool largeTitleText = f.backgroundRatio > 0.45 &&
                          f.whiteRatio > 0.38 &&
                          f.inkRatio < 0.55 &&
                          (f.edgeDensity > 0.045 || f.avgGradient > 14.0) &&
                          f.histEnergy > 0.020 &&
                          f.entropy < 5.65;
    return regularText || largeTitleText;
}

bool looks_like_photo(const BlockInfo& f, BlockClass cls) {
    if (plain_bg(f) || looks_like_text(f)) return false;
    if (cls == PHOTO) return true;
    if (cls == TEXT && f.backgroundRatio > 0.45 && f.whiteRatio > 0.38 && f.inkRatio < 0.55)
        return false;

    bool denseTones = f.inkRatio > 0.30 && (f.entropy > 2.55 || f.variance > 170.0);
    bool manyMidtones = f.midtoneRatio > 0.24 && f.variance > 85.0 && f.entropy > 2.35;
    bool notWhitePage = f.backgroundRatio < 0.58 && f.variance > 80.0;
    bool smoothPhotoPatch = f.midtoneRatio > 0.42 && f.histEnergy < 0.18 && f.backgroundRatio < 0.78;
    return denseTones || manyMidtones || notWhitePage || smoothPhotoPatch;
}

void join_photos(std::vector<BlockClass>& grid, const std::vector<BlockInfo>& features, int cols, int rows) {
    std::vector<bool> candidate(rows * cols, false);
    for (int i = 0; i < rows * cols; ++i)
        candidate[i] = looks_like_photo(features[i], grid[i]);

    std::vector<bool> visited(rows * cols, false);
    int dx[8] = {1,-1,0,0,1,1,-1,-1};
    int dy[8] = {0,0,1,-1,1,-1,1,-1};

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int start = r * cols + c;
            if (visited[start] || !candidate[start]) continue;

            std::vector<std::pair<int,int>> component;
            component.push_back({r, c});
            visited[start] = true;
            int head = 0;
            int minR = r, maxR = r, minC = c, maxC = c;
            double inkSum = 0.0, midtoneSum = 0.0;
            int initialPhoto = 0, textLike = 0;

            while (head < component.size()) {
                auto [cr, cc] = component[head++];
                int idx = cr * cols + cc;
                minR = std::min(minR, cr); maxR = std::max(maxR, cr);
                minC = std::min(minC, cc); maxC = std::max(maxC, cc);
                inkSum += features[idx].inkRatio;
                midtoneSum += features[idx].midtoneRatio;
                if (grid[idx] == PHOTO) initialPhoto++;
                if (looks_like_text(features[idx])) textLike++;

                for (int k = 0; k < 8; ++k) {
                    int nr = cr + dy[k], nc = cc + dx[k];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    int nidx = nr * cols + nc;
                    if (!visited[nidx] && candidate[nidx]) {
                        visited[nidx] = true;
                        component.push_back({nr, nc});
                    }
                }
            }

            int blockW = maxC - minC + 1;
            int blockH = maxR - minR + 1;
            int bboxArea = blockW * blockH;
            double density = (double)component.size() / bboxArea;
            double avgInk = inkSum / component.size();
            double avgMidtone = midtoneSum / component.size();
            double textLikeRatio = (double)textLike / component.size();

            bool largeEnough = (blockW >= 5 && blockH >= 3) ||
                               (blockW >= 3 && blockH >= 6) ||
                               (component.size() >= 40 && blockW >= 4 && blockH >= 3);
            bool photoEvidence = initialPhoto > 0 || avgInk > 0.18 || avgMidtone > 0.20;
            bool rectangularEnough = density > 0.16 || component.size() >= 40;
            bool mostlyNotText = textLikeRatio < 0.55 || avgInk > 0.34;

            if (!(largeEnough && photoEvidence && rectangularEnough && mostlyNotText))
                continue;

            for (int rr = minR; rr <= maxR; ++rr) {
                for (int cc = minC; cc <= maxC; ++cc) {
                    int idx = rr * cols + cc;
                    if (!plain_bg(features[idx]) || density > 0.35)
                        grid[idx] = PHOTO;
                }
            }
        }
    }
}

void clear_text_in_photo(std::vector<BlockClass>& grid, int cols, int rows) {
    std::vector<bool> visited(rows * cols, false);
    int dx[4] = {1, -1, 0, 0};
    int dy[4] = {0, 0, 1, -1};

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (visited[idx] || grid[idx] != TEXT) continue;

            std::vector<std::pair<int,int>> component;
            component.push_back({r, c});
            visited[idx] = true;
            int head = 0;
            int minR = r, maxR = r, minC = c, maxC = c;
            int surroundingPhoto = 0, surroundingBoundary = 0;

            while (head < component.size()) {
                auto [cr, cc] = component[head++];
                minR = std::min(minR, cr); maxR = std::max(maxR, cr);
                minC = std::min(minC, cc); maxC = std::max(maxC, cc);

                for (int k = 0; k < 4; ++k) {
                    int nr = cr + dy[k], nc = cc + dx[k];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    int nidx = nr * cols + nc;
                    if (grid[nidx] == TEXT && !visited[nidx]) {
                        visited[nidx] = true;
                        component.push_back({nr, nc});
                    } else if (grid[nidx] != TEXT) {
                        surroundingBoundary++;
                        if (grid[nidx] == PHOTO) surroundingPhoto++;
                    }
                }
            }

            int blockW = maxC - minC + 1;
            int blockH = maxR - minR + 1;
            bool enclosedByPhoto = surroundingBoundary > 0 && surroundingPhoto >= surroundingBoundary * 0.55;
            bool smallOrNarrow = component.size() <= 22 || blockW <= 2 || blockH <= 2;

            if (enclosedByPhoto && smallOrNarrow) {
                for (auto& p : component)
                    grid[p.first * cols + p.second] = PHOTO;
            }
        }
    }
}

void clear_small_photos(std::vector<BlockClass>& grid, int cols, int rows) {
    std::vector<bool> visited(rows * cols, false);
    int dx[4] = {1, -1, 0, 0};
    int dy[4] = {0, 0, 1, -1};

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (visited[idx] || grid[idx] != PHOTO) continue;

            std::vector<std::pair<int,int>> component;
            component.push_back({r, c});
            visited[idx] = true;
            int head = 0;
            int minR = r, maxR = r, minC = c, maxC = c;

            while (head < component.size()) {
                auto [cr, cc] = component[head++];
                minR = std::min(minR, cr); maxR = std::max(maxR, cr);
                minC = std::min(minC, cc); maxC = std::max(maxC, cc);

                for (int k = 0; k < 4; ++k) {
                    int nr = cr + dy[k], nc = cc + dx[k];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    int nidx = nr * cols + nc;
                    if (grid[nidx] == PHOTO && !visited[nidx]) {
                        visited[nidx] = true;
                        component.push_back({nr, nc});
                    }
                }
            }

            int blockW = maxC - minC + 1;
            int blockH = maxR - minR + 1;
            bool tooSmallForPhoto = component.size() <= 12 || (blockW <= 2 && blockH <= 8) || (blockH <= 2 && blockW <= 8);
            if (tooSmallForPhoto) {
                for (auto& p : component)
                    grid[p.first * cols + p.second] = TEXT;
            }
        }
    }
}

std::vector<Rect> make_rects(const std::vector<BlockClass>& grid, int cols, int rows, int blockSize, int imgW, int imgH) {
    std::vector<bool> visited(rows * cols, false);
    std::vector<Rect> rects;
    int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (visited[idx] || grid[idx] == BG) continue;
            std::vector<std::pair<int,int>> q; q.push_back({r, c}); visited[idx] = true;
            int head = 0;
            while (head < q.size()) {
                auto [cr, cc] = q[head++];
                for (int k = 0; k < 4; ++k) {
                    int nr = cr + dy[k], nc = cc + dx[k];
                    if (nr >= 0 && nr < rows && nc >= 0 && nc < cols) {
                        int nidx = nr * cols + nc;
                        if (!visited[nidx] && grid[nidx] == grid[idx]) { visited[nidx] = true; q.push_back({nr, nc}); }
                    }
                }
            }
            int minR = rows, maxR = -1, minC = cols, maxC = -1;
            for (auto& p : q) { minR = std::min(minR, p.first); maxR = std::max(maxR, p.first); minC = std::min(minC, p.second); maxC = std::max(maxC, p.second); }
            Rect rec;
            rec.x = minC * blockSize; rec.y = minR * blockSize;
            rec.w = (maxC - minC + 1) * blockSize; rec.h = (maxR - minR + 1) * blockSize;
            rec.cls = grid[idx];
            if (rec.x + rec.w > imgW) rec.w = imgW - rec.x;
            if (rec.y + rec.h > imgH) rec.h = imgH - rec.y;
            if (rec.cls == PHOTO && rec.w <= blockSize && rec.h <= blockSize) continue;
            if (rec.cls == PHOTO && rec.w <= blockSize * 2 && rec.h <= blockSize * 2 && rec.w * rec.h < blockSize * blockSize * 3) continue;
            if (rec.w > 10 && rec.h > 10) rects.push_back(rec); // отсев артефактов
        }
    }
    return rects;
}

int main() {
    std::string inputTiff = "17.tif";
    std::string outDir = "results/";
    
    TifReader reader;
    std::vector<uint8_t> gray; uint32_t W, H;
    if (!reader.load_gray(inputTiff, gray, W, H)) {
        std::cerr << "[ERROR] Failed to load TIFF.\n"; return 1;
    }

    cv::Mat imgGray(H, W, CV_8UC1, gray.data());
    cv::imwrite(outDir + "00_original_read.png", imgGray);

    unsigned long hist[256]; histogram(gray.data(), W, H, hist);
    int T_otsu = otsu(hist, W * H);
    std::vector<uint8_t> mask(W * H);
    for (int i = 0; i < W * H; ++i) mask[i] = (gray[i] > T_otsu) ? 255 : 0;
    cv::imwrite(outDir + "01_otsu_mask.png", cv::Mat(H, W, CV_8UC1, mask.data()));

    std::vector<uint8_t> grad; sobel(gray.data(), W, H, grad);

    const int BS = 32;
    int cols = (W + BS - 1) / BS, rows = (H + BS - 1) / BS;
    std::vector<BlockClass> grid(rows * cols);
    std::vector<BlockInfo> features(rows * cols);

    for (int by = 0; by < H; by += BS)
        for (int bx = 0; bx < W; bx += BS) {
            int bw = std::min(BS, (int)W - bx), bh = std::min(BS, (int)H - by);
            int r = by / BS, c = bx / BS;
            int idx = r * cols + c;
            features[idx] = block_info(gray.data(), grad.data(), mask.data(), bx, by, bw, bh, W);
            grid[idx] = get_class(features[idx]);
        }

    smooth(grid, cols, rows);
    clear_photo_noise(grid, cols, rows);
    join_photos(grid, features, cols, rows);
    clear_text_in_photo(grid, cols, rows);
    smooth(grid, cols, rows);
    clear_text_in_photo(grid, cols, rows);
    clear_photo_noise(grid, cols, rows);
    clear_small_photos(grid, cols, rows);

    cv::Mat classMap(H, W, CV_8UC3, cv::Scalar(0,0,0));
    for (int i = 0; i < grid.size(); ++i) {
        int r = i / cols, c = i % cols;
        cv::Scalar color = (grid[i] == TEXT) ? cv::Scalar(255,0,0) : (grid[i] == PHOTO) ? cv::Scalar(0,255,0) : cv::Scalar(128,128,128);
        int x = c * BS, y = r * BS;
        classMap(cv::Rect(x, y, std::min(BS, (int)W-x), std::min(BS, (int)H-y))).setTo(color);
    }
    cv::imwrite(outDir + "02_classification_map.png", classMap);

    cv::Mat backgroundMap; cv::cvtColor(imgGray, backgroundMap, cv::COLOR_GRAY2BGR);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int blockRow = y / BS, blockCol = x / BS;
            int idx = blockRow * cols + blockCol;
            cv::Vec3b& p = backgroundMap.at<cv::Vec3b>(y, x);
            if (grid[idx] == BG) {
                p[0] = static_cast<uint8_t>(p[0] * 0.35);
                p[1] = static_cast<uint8_t>(std::min(255, (int)(p[1] * 0.55) + 115));
                p[2] = static_cast<uint8_t>(std::min(255, (int)(p[2] * 0.55) + 115));
            } else {
                p[0] = static_cast<uint8_t>(p[0] * 0.35);
                p[1] = static_cast<uint8_t>(p[1] * 0.35);
                p[2] = static_cast<uint8_t>(p[2] * 0.35);
            }
        }
    }
    cv::imwrite(outDir + "03_background_marked.png", backgroundMap);

    std::vector<Rect> rects = make_rects(grid, cols, rows, BS, W, H);
    cv::Mat finalImg; cv::cvtColor(imgGray, finalImg, cv::COLOR_GRAY2BGR);
    for (const auto& rec : rects) {
        cv::Scalar col = (rec.cls == TEXT) ? cv::Scalar(255, 0, 0) : cv::Scalar(0, 255, 0);
        cv::rectangle(finalImg, cv::Point(rec.x, rec.y), cv::Point(rec.x + rec.w, rec.y + rec.h), col, 2);
        cv::putText(finalImg, rec.cls == TEXT ? "TEXT" : "PHOTO", cv::Point(rec.x, rec.y - 5), cv::FONT_HERSHEY_SIMPLEX, 0.4, col, 1);
    }
    cv::imwrite(outDir + "04_final_rectangles.png", finalImg);
    std::cout << "Done. Results in " << outDir << "\n";
    return 0;
}
