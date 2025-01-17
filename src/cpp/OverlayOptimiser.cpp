//
// This file is part of OverlayPal ( https://github.com/michel-iwaniec/OverlayPal )
// Copyright (c) 2021 Michel Iwaniec.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <streambuf>
#include <sstream>
#include <cstdio>
#include <array>
#include <functional>
#include <vector>

#include "SubProcess.h"

#include "OverlayOptimiser.h"

//---------------------------------------------------------------------------------------------------------------------

OverlayOptimiser::OverlayOptimiser():
    mBackgroundColor(0),
    mSpriteHeight(16)
{

}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::setExecutablePath(const std::string& executablePath)
{
    mExecutablePath = executablePath;
}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::setWorkPath(const std::string& workPath)
{
    mWorkPath = workPath;
}

//---------------------------------------------------------------------------------------------------------------------

std::string OverlayOptimiser::exePathFilename(const std::string& exeFilename) const
{
    return mExecutablePath + "/" + exeFilename;
}

//---------------------------------------------------------------------------------------------------------------------

std::string OverlayOptimiser::workPathFilename(const std::string& workFilename) const
{
    return mWorkPath + "/" + workFilename;
}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::writeCmplLayerData(std::ofstream& f, const std::string& name, const GridLayer& layer, std::function<int(int, int, int)> const& callback)
{
    f << "%" << name.c_str() << "[XRANGE, YRANGE, COLORS] <\n";
    for(int x = 0; x < layer.width(); x++)
    {
        for(int y = 0; y < layer.height(); y++)
        {
            for(auto c : layer.colors())
            {
                int v = callback(x, y, c);
                f << v << " ";
            }
            f << "\n";
        }
    }
    f << ">\n";
}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::writeCmplDataFile(const GridLayer& layer, int gridCellColorLimit, int maxBackgroundPalettes, int maxSpritePalettes, int maxRowSize, const std::string& filename)
{
    std::ofstream f(filename, std::ofstream::out);
    if(!f)
    {
        throw std::runtime_error(std::string("Failed to open file '") + filename + "' for writing CMPL input data.");
    }
    // Limits
    f << "%CELL_COLOR_LIMIT < " << gridCellColorLimit << " >\n";
    f << "%MAX_BG_PALETTES < " << maxBackgroundPalettes << " >\n";
    f << "%BG_PALETTES set < 0.." << maxBackgroundPalettes-1 << " >\n";
    f << "%MAX_SPR_PALETTES < " << maxSpritePalettes << " >\n";
    f << "%SPR_PALETTES set < 0.." << maxSpritePalettes-1 << " >\n";
    f << "%OVERLAY_ROW_SIZE_LIMIT < " << maxRowSize << " >\n";
    // X / Y ranges
    f << "%XRANGE set < 0.." << layer.width()-1 << " >\n";
    f << "%YRANGE set < 0.." << layer.height()-1 << " >\n";
    // All colors present in layer
    f << "%COLORS set < ";
    for(auto c : layer.colors())
    {
        f << int(c) << " ";
    }
    f << " >\n";
    // layerColors
    writeCmplLayerData(f, "layerColors", layer, [&](int x, int y, uint8_t c) { return layer(x, y).colors.count(c) ? 1 : 0; });
    writeCmplLayerData(f, "layerColorColumnCount", layer, [&](int x, int y, uint8_t c) { return layer(x, y).colors.count(c) ? layer(x, y).columnCount.at(c) : 0; });
}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::runCmplProgram(const std::string& inputFilename,
                                      const std::string& outputFilename,
                                      const std::string& solutionCsvFilename,
                                      int timeOut)
{
    // Make a copy of the original program and prepend timeOut parameter to it
    // This is an ugly work-around for there being no other way(?) to set the CBC timeout parameter :(
    std::ifstream inputFile(inputFilename, std::ifstream::in);
    std::string inputFileStr((std::istreambuf_iterator<char>(inputFile)),
                              std::istreambuf_iterator<char>());
    inputFile.close();
    std::ofstream outputFile(outputFilename);
    if(timeOut)
    {
        outputFile << "%opt cbc seconds " << timeOut << "\n";
    }
    outputFile << inputFileStr;
    outputFile.close();
    // Execute process
    std::string cmplExecutable("Cmpl/bin/cmpl");
    std::vector<std::string> params;
    params.push_back("-i");
    params.push_back(quoteStringOnWindows(outputFilename));
    params.push_back("-solutionCsv");
    params.push_back(quoteStringOnWindows(solutionCsvFilename));
    int exitCode = executeProcess(exePathFilename(cmplExecutable), params, timeOut, mWorkPath);
    if(exitCode != 0)
    {
        throw Error("Non-zero exit code from CMPL");
    }
}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::parseSolutionValue(const std::string& line, std::vector<int>& indices, int& value)
{
    indices.clear();
    size_t arrayStartPos = line.find("[", 0);
    size_t arrayEndPos = line.find("]", 0);
    size_t activityPos = line.find(";B;", 0);
    if(activityPos == std::string::npos)
    {
        // For currently unknown reasons, the CMPL solution will sometimes have
        // binary variables changed to integer.
        // Work around this bug by re-trying a second time with 'I' instead of 'B'.
        activityPos = line.find(";I;", 0);
    }
    if(arrayStartPos != std::string::npos &&
       arrayEndPos != std::string::npos &&
       activityPos != std::string::npos)
    {
        assert(arrayStartPos+1 < line.size());
        assert(arrayEndPos+1 < line.size());
        assert(activityPos+3 < line.size());
        assert(arrayStartPos < arrayEndPos);
        // Read indices
        {
            std::string indicesStr = line.substr(arrayStartPos + 1, arrayEndPos - arrayStartPos + 1); //arrayStartPos
            char c;
            int i;
            std::stringstream ss(indicesStr);
            while(true)
            {
                ss >> i >> c;
                indices.push_back(i);
                if(c != ',')
                    break;
            }
        }
        // Read activity
        {
            std::string activityStr = line.substr(activityPos+3, line.size() - (activityPos + 3));
            std::stringstream ss(activityStr);
            ss >> value;
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------

bool OverlayOptimiser::parseCmplSolution(const std::string& csvFilename,
                                         std::vector<std::set<uint8_t>>& palettes,
                                         GridLayer& colorsBackground,
                                         GridLayer& colorsOverlay,
                                         Array2D<uint8_t>& paletteIndicesBackground,
                                         bool secondPass)
{
    std::ifstream f;
    f.open(csvFilename, std::ifstream::in);
    if(!f)
    {
        throw std::runtime_error(std::string("Failed to open solution file: ") + csvFilename);
    }
    // Read data
    const std::string noSolutionString = "No solution has been found";
    const std::string colorsBackgroundPrefix = secondPass ? "colorsOverlayGrid[" : "colorsBG[";
    const std::string colorsOverlayPrefix = secondPass ? "colorsOverlayFree[" : "colorsOverlay[";
    const std::string palettesNamePrefix = secondPass ? "palettesOverlay[" : "palettesBG[";
    const std::string usesPalettePrefix = secondPass ? "usesPaletteOverlay[" : "usesPaletteBG[";
    std::string line;
    std::getline(f, line);
    if(line.find("Problem;") == std::string::npos)
    {
        throw std::runtime_error(std::string("Solution file header unrecognized"));
    }
    // Parse values
    palettes.clear();
    std::vector<int> indices;
    int value;
    while(true)
    {
        if(!std::getline(f, line))
            break;
        if(line.find(noSolutionString, 0) == 0)
        {
            throw std::runtime_error(std::string("No solution found"));
        }
        else if(line.rfind(colorsBackgroundPrefix, 0) == 0)
        {
            parseSolutionValue(line, indices, value);
            assert(indices.size() == 3 && "colors background index length mismatch");
            if(value == 1)
            {
                int x = indices[0];
                int y = indices[1];
                int c = indices[2];
                colorsBackground(x, y).colors.insert(c);
            }
        }
        else if(line.rfind(colorsOverlayPrefix, 0) == 0)
        {
            parseSolutionValue(line, indices, value);
            assert(indices.size() == 3 && "colors overlay index length mismatch");
            if(value == 1)
            {
                int x = indices[0];
                int y = indices[1];
                int c = indices[2];
                colorsOverlay(x, y).colors.insert(c);
            }
        }
        else if(line.rfind(palettesNamePrefix, 0) == 0)
        {
            parseSolutionValue(line, indices, value);
            assert(indices.size() == 2 && "Palette index length mismatch");
            if(value == 1)
            {
                int p = indices[0];
                int c = indices[1];
                if(p >= palettes.size())
                {
                    palettes.resize(p + 1);
                }
                palettes[p].insert(c);
            }
        }
        else if(line.rfind(usesPalettePrefix, 0) == 0)
        {
            parseSolutionValue(line, indices, value);
            assert(indices.size() == 3 && "Uses-palette index length mismatch");
            if(value == 1)
            {
                int x = indices[0];
                int y = indices[1];
                int paletteIndex = indices[2];
                paletteIndicesBackground(x, y) = paletteIndex + (secondPass ? NumBackgroundPalettes : 0);
            }
        }
    }
    return true;
}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::setEmptyPaletteIndices(Array2D<uint8_t>& paletteIndices, const GridLayer& layer, uint8_t emptyIndex)
{
    assert(paletteIndices.width() == layer.width() && paletteIndices.height() == layer.height());
    for(size_t y = 0; y < layer.height(); y++)
    {
        for(size_t x = 0; x < layer.width(); x++)
        {
            if(layer(x, y).colors.size() == 0)
            {
                paletteIndices(x, y) = emptyIndex;
            }
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------

const Array2D<uint8_t> &OverlayOptimiser::debugPaletteIndicesBackground() const
{
    return mPaletteIndicesBackground;
}

//---------------------------------------------------------------------------------------------------------------------

const GridLayer &OverlayOptimiser::layerBackground() const
{
    return mLayerBackground;
}

//---------------------------------------------------------------------------------------------------------------------

const GridLayer &OverlayOptimiser::layerOverlay() const
{
    return mLayerOverlay;
}

//---------------------------------------------------------------------------------------------------------------------

void moveOverlayColors(const Image2D& inputImage,
                  Image2D& imageBackground,
                  Image2D& imageOverlay,
                  GridLayer& layerOverlay,
                  uint8_t backgroundColor)
{
    int cellWidth = layerOverlay.cellWidth();
    int cellHeight = layerOverlay.cellHeight();
    for(size_t y = 0; y < layerOverlay.height(); y++)
    {
        for(size_t x = 0; x < layerOverlay.width(); x++)
        {
            for(size_t i = 0; i < layerOverlay.cellHeight(); i++)
            {
                for(size_t j = 0; j < layerOverlay.cellWidth(); j++)
                {
                    int xx = x * cellWidth + j;
                    int yy = y * cellHeight + i;
                    uint8_t c = inputImage(xx, yy);
                    imageOverlay(xx, yy) = backgroundColor;
                    imageBackground(xx, yy) = inputImage(xx, yy);
                    if(layerOverlay(x, y).colors.count(c) > 0)
                    {
                        imageOverlay(xx, yy) = c;
                        imageBackground(xx, yy) = backgroundColor;
                    }
                }
            }
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------

bool OverlayOptimiser::consistentLayers(const Image2D& image, const GridLayer& layer, const std::vector<std::set<uint8_t>>& palettes, const Array2D<uint8_t>& paletteIndices, uint8_t backgroundColor)
{
    assert(layer.width() == paletteIndices.width() && layer.height() == paletteIndices.height());
    const size_t w = layer.width();
    const size_t h = layer.height();
    for(size_t y = 0; y < h; y++)
    {
        for(size_t x = 0; x < w; x++)
        {
            uint8_t paletteIndex = paletteIndices(x, y);
            assert(paletteIndex < palettes.size());
            for(uint8_t c : layer(x, y).colors)
            {
                bool colorInPalette = palettes[paletteIndex].count(c) > 0;
                assert(colorInPalette && "GridCell colors must be present in palette");
                if(!colorInPalette)
                    return false;
            }
            //
            for(size_t i = 0; i < layer.cellHeight(); i++)
            {
                for(size_t j = 0; j < layer.cellWidth(); j++)
                {
                    size_t xx = layer.cellWidth() * x + j;
                    size_t yy = layer.cellHeight() * y + i;
                    uint8_t c = image(xx, yy);
                    if(c != backgroundColor)
                    {
                        bool colorInGridCell = layer(x, y).colors.count(c) > 0;
                        assert(colorInGridCell && "Pixel colors must be present in grid cell");
                        if(!colorInGridCell)
                            return false;
                    }
                }
            }
        }
    }
    return true;
}

//---------------------------------------------------------------------------------------------------------------------

void OverlayOptimiser::fillMissingPaletteGroups(std::vector<std::set<uint8_t>>& palettes, size_t numPalettes)
{
    while(palettes.size() < numPalettes)
    {
        palettes.push_back(std::set<uint8_t>());
    }
}

//---------------------------------------------------------------------------------------------------------------------

bool OverlayOptimiser::convertFirstPassNoBG(int gridCellColorLimit,
                                            int maxSpritePalettes,
                                            int maxRowSize,
                                            const GridLayer& layer,
                                            GridLayer& layerBackground,
                                            GridLayer& layerOverlay,
                                            std::vector<std::set<uint8_t>>& palettesBG,
                                            Array2D<uint8_t>& paletteIndicesBackground)
{
    std::set<uint8_t> colors;
    int maxCellsInRow = 0;
    for(int y = 0; y < layer.height(); y++)
    {
        int numCellsInRow = 0;
        for(int x = 0; x < layer.width(); x++)
        {
            colors.insert(layer(x, y).colors.begin(), layer(x, y).colors.end());
            if(layer(x, y).colors.size() > 0)
                numCellsInRow++;
            layerOverlay(x, y) = layer(x, y);
            layerBackground(x, y) = GridCell();
        }
        maxCellsInRow = std::max(maxCellsInRow, numCellsInRow);
    }
    setEmptyPaletteIndices(paletteIndicesBackground, layerBackground, 0);
    const int maxColorsOverlay = maxSpritePalettes * gridCellColorLimit;
    return colors.size() <= maxColorsOverlay && maxCellsInRow <= maxRowSize;
}

//---------------------------------------------------------------------------------------------------------------------

bool OverlayOptimiser::convertFirstPass(const Image2D& image,
                                        int gridCellColorLimit,
                                        int maxBackgroundPalettes,
                                        int maxSpritePalettes,
                                        int maxRowSize,
                                        int timeOut,
                                        const GridLayer& layer,
                                        GridLayer& layerBackground,
                                        GridLayer& layerOverlay,
                                        std::vector<std::set<uint8_t>>& palettesBG,
                                        Array2D<uint8_t>& paletteIndicesBackground)
{
    // Special-case for maxBackgroundPalettes = 0
    if(maxBackgroundPalettes == 0)
    {
        if(!convertFirstPassNoBG(gridCellColorLimit,
                                 maxSpritePalettes,
                                 maxRowSize,
                                 layer,
                                 layerBackground,
                                 layerOverlay,
                                 palettesBG,
                                 paletteIndicesBackground))
        {
            throw Error("First pass of no-background conversion failed.");
        }
        else
        {
            return true;
        }
    }
    // Make layer for input image
    writeCmplDataFile(layer,
                      gridCellColorLimit,
                      maxBackgroundPalettes,
                      maxSpritePalettes,
                      maxRowSize,
                      workPathFilename(firstPassDataFilename));
    //
    runCmplProgram(exePathFilename(firstPassProgramInputFilename),
                   workPathFilename(firstPassProgramOutputFilename),
                   workPathFilename(firstPassSolutionFilename),
                   timeOut);
    if(!parseCmplSolution(workPathFilename(firstPassSolutionFilename),
                          palettesBG,
                          layerBackground,
                          layerOverlay,
                          paletteIndicesBackground,
                          false))
    {
        throw Error("Failed to parse CMPL result (first pass)");
    }
    setEmptyPaletteIndices(paletteIndicesBackground, layerBackground, 0);
    return true;
}

//---------------------------------------------------------------------------------------------------------------------

bool OverlayOptimiser::convertSecondPass(int gridCellColorLimit,
                                         int maxSpritePalettes,
                                         int maxSpritesPerScanline,
                                         int timeOut,
                                         const GridLayer& layer,
                                         GridLayer& layerOverlayGrid,
                                         GridLayer& layerOverlayFree,
                                         std::vector<std::set<uint8_t>>& palettes,
                                         Array2D<uint8_t>& paletteIndicesOverlay)
{
    writeCmplDataFile(layer,
                      gridCellColorLimit,
                      0,
                      maxSpritePalettes,
                      2 * maxSpritesPerScanline,
                      workPathFilename(secondPassDataFilename));
    //
    runCmplProgram(exePathFilename(secondPassProgramInputFilename),
                   workPathFilename(secondPassProgramOutputFilename),
                   workPathFilename(secondPassSolutionFilename),
                   timeOut);
    std::vector<std::set<uint8_t>> palettesSPR;
    if(!parseCmplSolution(workPathFilename(secondPassSolutionFilename),
                          palettesSPR,
                          layerOverlayGrid,
                          layerOverlayFree,
                          paletteIndicesOverlay,
                          true))
    {
        throw Error("Failed to parse CMPL result (second pass)");
    }
    setEmptyPaletteIndices(paletteIndicesOverlay, layerOverlayGrid, NumBackgroundPalettes);
    for(const std::set<uint8_t>& palette : palettesSPR)
    {
        palettes.push_back(palette);
    }
    return true;
}

//---------------------------------------------------------------------------------------------------------------------

std::string OverlayOptimiser::convert(const Image2D& image,
                                      uint8_t backgroundColor,
                                      int gridCellWidth,
                                      int gridCellHeight,
                                      int _spriteHeight,
                                      int gridCellColorLimit,
                                      int maxBackgroundPalettes,
                                      int maxSpritePalettes,
                                      int maxSpritesPerScanline,
                                      int timeOut)
{
    // Remove old files
    std::array<const char*, 6> filenames = {
        firstPassProgramOutputFilename,
        firstPassDataFilename,
        firstPassSolutionFilename,
        secondPassProgramOutputFilename,
        secondPassDataFilename,
        secondPassSolutionFilename
    };
    for(const char* filename : filenames)
    {
        remove(workPathFilename(filename).c_str());
    }
    mBackgroundColor = backgroundColor;
    mSpriteHeight = _spriteHeight;
    Image2D imageBackground(image.width(), image.height());
    Image2D imageOverlay(image.width(), image.height());
    Image2D imageOverlayGrid(image.width(), image.height());
    Image2D imageOverlayFree(image.width(), image.height());
    GridLayer layer(mBackgroundColor, gridCellWidth, gridCellHeight, image);
    GridLayer layerBackground(mBackgroundColor, layer.cellWidth(), layer.cellHeight(), layer.width(), layer.height());
    GridLayer layerOverlay(mBackgroundColor, layer.cellWidth(), layer.cellHeight(), layer.width(), layer.height());
    Array2D<uint8_t> paletteIndicesBackground(layer.width(), layer.height());
    const int OverlayGridCellWidth = spriteWidth();
    const int OverlayGridCellHeight = _spriteHeight;
    const int OverlayWidth = image.width() / OverlayGridCellWidth;
    const int OverlayHeight = image.height() / OverlayGridCellHeight;
    // Initialise output data to blank values
    const Image2D blankImage = Image2D(image.width(), image.height(), mBackgroundColor);
    mOutputImage = blankImage;
    mOutputImageBackground = blankImage;
    mOutputImageOverlay = blankImage;
    mOutputImageOverlayGrid = blankImage;
    mOutputImageOverlayFree = blankImage;
    const GridLayer blankOverlay = GridLayer(mBackgroundColor, OverlayGridCellWidth, OverlayGridCellHeight, OverlayWidth, OverlayHeight);
    mLayerOverlay = blankOverlay;
    mLayerOverlayFree = blankOverlay;
    mPaletteIndicesBackground = Array2D<uint8_t>(layer.width(), layer.height());
    mPaletteIndicesOverlay = Array2D<uint8_t>(OverlayWidth, OverlayHeight);
    // * 4 to always get a visible solution, even if beyond constraints
    int maxRowSize = ((4 * spriteWidth()) / gridCellWidth) * maxSpritesPerScanline;
    // Execute first pass
    std::vector<std::set<uint8_t>> palettes;
    bool successPassOne = convertFirstPass(image,
                                           gridCellColorLimit,
                                           maxBackgroundPalettes,
                                           maxSpritePalettes,
                                           maxRowSize,
                                           timeOut,
                                           layer,
                                           layerBackground,
                                           layerOverlay,
                                           palettes,
                                           paletteIndicesBackground);
    if(!successPassOne)
        return "First pass failed.";
    // Optimize easily-fixable bad cases that originate from solver getting timed out
    optimizeUnnecessaryOverlayColors(layerBackground,
                                     layerOverlay,
                                     paletteIndicesBackground,
                                     0,
                                     palettes);
    // Merge palettes when possible
    optimizeUnnecessaryPalettes(paletteIndicesBackground,
                                0,
                                palettes,
                                gridCellColorLimit);
    fillMissingPaletteGroups(palettes, NumBackgroundPalettes);
    // Split image into background and overlay
    moveOverlayColors(image, imageBackground, imageOverlay, layerOverlay, backgroundColor);
    optimizeContinuity(layerBackground, paletteIndicesBackground, 0, palettes, backgroundColor);
    assert(consistentLayers(imageBackground, layerBackground, palettes, paletteIndicesBackground, backgroundColor));
    assert(!image.empty(mBackgroundColor));
    assert(!imageBackground.empty(mBackgroundColor) || maxBackgroundPalettes == 0);
    mOutputImageBackground = imageBackground;
    mLayerBackground = layerBackground;
    // if no colors were moved into overlay we are done
    if(imageOverlay.empty(mBackgroundColor) || maxSpritePalettes == 0)
    {
        mOutputImage = image;
        mPaletteIndicesBackground = paletteIndicesBackground;
        for(size_t i = 0; i < NumSpritePalettes; i++)
        {
            std::set<uint8_t> palette;
            palettes.push_back(palette);
        }
        mPalettes = palettes;
        if(imageOverlay.empty(mBackgroundColor))
            return "";
        else
            return "Sprite palettes required.";
    }
    // Re-initialise overlay layer with /2 width (...and /2 height if using 8x8 sprites)
    layerOverlay = GridLayer(backgroundColor, OverlayGridCellWidth, OverlayGridCellHeight, imageOverlay);
    GridLayer layerOverlayGrid(backgroundColor, OverlayGridCellWidth, OverlayGridCellHeight, OverlayWidth, OverlayHeight);
    GridLayer layerOverlayFree(backgroundColor, OverlayGridCellWidth, OverlayGridCellHeight, OverlayWidth, OverlayHeight);
    Array2D<uint8_t> paletteIndicesOverlay(OverlayWidth, OverlayHeight);
    // Second pass
    bool successPassTwo = convertSecondPass(gridCellColorLimit,
                                            maxSpritePalettes,
                                            4 * maxSpritesPerScanline,
                                            timeOut,
                                            layerOverlay,
                                            layerOverlayGrid,
                                            layerOverlayFree,
                                            palettes,
                                            paletteIndicesOverlay);
    if(!successPassTwo)
        throw Error("Second pass failed.");
    // Optimize easily-fixable bad cases that originate from solver getting timed out
    optimizeUnnecessaryOverlayColors(layerOverlayGrid,
                                     layerOverlayFree,
                                     paletteIndicesOverlay,
                                     NumBackgroundPalettes,
                                     palettes);
    // Merge palettes when possible
    optimizeUnnecessaryPalettes(paletteIndicesOverlay,
                                NumBackgroundPalettes,
                                palettes,
                                gridCellColorLimit);
    fillMissingPaletteGroups(palettes, NumBackgroundPalettes+NumSpritePalettes);
    moveOverlayColors(imageOverlay, imageOverlayGrid, imageOverlayFree, layerOverlayFree, backgroundColor);
    optimizeContinuity(layerOverlayGrid, paletteIndicesOverlay, NumBackgroundPalettes, palettes, backgroundColor);
    assert(consistentLayers(imageOverlayGrid, layerOverlayGrid, palettes, paletteIndicesOverlay, backgroundColor));
    // Copy state to persistent members
    mLayerBackground = layerBackground;
    mLayerOverlay = layerOverlayGrid;
    mLayerOverlayFree = layerOverlayFree;
    mPaletteIndicesBackground = paletteIndicesBackground;
    mPaletteIndicesOverlay = paletteIndicesOverlay;
    assert(!image.empty(mBackgroundColor));
    assert(!imageBackground.empty(mBackgroundColor) || maxBackgroundPalettes == 0);
    assert(!imageOverlay.empty(mBackgroundColor));
    mOutputImage = image;
    mOutputImageOverlayGrid = imageOverlayGrid;
    mOutputImageOverlayFree = imageOverlayFree;
    assert(!mOutputImage.empty(mBackgroundColor));
    assert(!mOutputImageBackground.empty(mBackgroundColor) || maxBackgroundPalettes == 0);
    mPalettes = palettes;
    // Finally, return error if maxSpritesPerScanline boundary not met
    if(getMaxSpritesPerScanline(spritesOverlay()) > maxSpritesPerScanline)
        return "Too many sprites / scanline";
    else
        return "";
}

//---------------------------------------------------------------------------------------------------------------------

bool OverlayOptimiser::conversionSuccessful() const
{
    return mConversionSuccessful;
}

//---------------------------------------------------------------------------------------------------------------------

Image2D OverlayOptimiser::outputImageBackground() const
{
    assert(!mOutputImage.empty(mBackgroundColor));
    return remapColors(mOutputImageBackground, mLayerBackground, mPalettes, mPaletteIndicesBackground);
}

//---------------------------------------------------------------------------------------------------------------------

Image2D OverlayOptimiser::outputImageOverlayGrid() const
{
    assert(!mOutputImage.empty(mBackgroundColor));
    return remapColors(mOutputImageOverlayGrid, mLayerOverlay, mPalettes, mPaletteIndicesOverlay);
}

//---------------------------------------------------------------------------------------------------------------------

Image2D OverlayOptimiser::outputImageOverlayFree() const
{
    auto sprites = spritesOverlayFree();
    // Write sprites to new image
    Image2D outputImage(mOutputImageOverlayFree.width(), mOutputImageOverlayFree.height());
    for(auto const& s : sprites)
    {
        for(size_t y = 0; y < spriteHeight(); y++)
        {
            for(size_t x = 0; x < spriteWidth(); x++)
            {
                uint8_t c = s.pixels(x, y);
                if(c != mBackgroundColor)
                {
                    outputImage(s.x + x, s.y + y) = (s.p << 2) | indexInPalette(mPalettes[s.p], c);
                }
            }
        }
    }
    return outputImage;
}

//---------------------------------------------------------------------------------------------------------------------

Image2D OverlayOptimiser::outputImage() const
{
    assert(!mOutputImage.empty(mBackgroundColor));
    Image2D background = outputImageBackground();
    Image2D overlayGrid = outputImageOverlayGrid();
    Image2D overlayFree = outputImageOverlayFree();
    assert(background.width() == overlayGrid.width() && background.height() == overlayGrid.height());
    assert(background.width() == overlayFree.width() && background.height() == overlayFree.height());
    size_t w = background.width();
    size_t h = background.height();
    Image2D image(w, h);
    for(size_t y = 0; y < h; y++)
    {
        for(size_t x = 0; x < w; x++)
        {
            uint8_t cB = background(x, y);
            uint8_t cOG = overlayGrid(x, y);
            uint8_t cOF = overlayFree(x, y);
            assert(!((cB != 0) && (cOG != 0) && (cOF != 0)));
            if(cOF != 0)
            {
                image(x, y) = cOF;
            }
            else if(cOG != 0)
            {
                image(x, y) = cOG;
            }
            else if(cB != 0)
            {
                image(x, y) = cB;
            }
            else
            {
                image(x, y) = 0;
            }
        }
    }
    return image;
}

//---------------------------------------------------------------------------------------------------------------------

Image2D OverlayOptimiser::remapColors(const Image2D& image,
                                      const GridLayer& layer,
                                      const std::vector<std::set<uint8_t>>& palettes,
                                      const Array2D<uint8_t>& paletteIndices) const
{
    // Create per-cell mapping
    size_t gridWidth = paletteIndices.width();
    size_t gridHeight = paletteIndices.height();
    Array2D<std::unordered_map<uint8_t, uint8_t>> perCellMappingForward(gridWidth, gridHeight);
    for(size_t y = 0; y < gridHeight; y++)
    {
        for(size_t x = 0; x < gridWidth; x++)
        {
            uint8_t paletteIndex = paletteIndices(x, y);
            size_t j = 1;
            for(uint8_t c : palettes[paletteIndex])
            {
                perCellMappingForward(x, y)[c] = paletteIndex * PaletteGroupSize + j;
                j++;
            }
        }
    }
    // Map each pixel
    Image2D rImage(image.width(), image.height());
    for(size_t y = 0; y < image.height(); y++)
    {
        for(size_t x = 0; x < image.width(); x++)
        {
            size_t cellX = x / layer.cellWidth();
            size_t cellY = y / layer.cellHeight();
            const std::unordered_map<uint8_t, uint8_t>& m = perCellMappingForward(cellX, cellY);
            uint8_t c = image(x, y);
            if(m.find(c) != m.end())
            {
                c = m.at(c);
                rImage(x, y) = c;
            }
            else
            {
                assert(c == mBackgroundColor);
                rImage(x, y) = 0;
            }
        }
    }
    return rImage;
}

//---------------------------------------------------------------------------------------------------------------------

const std::vector<std::set<uint8_t>> &OverlayOptimiser::palettes() const
{
    return mPalettes;
}

//---------------------------------------------------------------------------------------------------------------------

Sprite OverlayOptimiser::extractSpriteWithBestPalette(Image2D& overlayImage,
                                                      size_t x,
                                                      size_t y,
                                                      size_t width,
                                                      size_t height,
                                                      bool removePixels) const
{
    // Try extracting sprites for each palette, and keep track of best one (the one extracting most colors)
    size_t bestIndex = 0;
    size_t bestMaxColors = 0;
    for(size_t i = NumBackgroundPalettes; i < NumBackgroundPalettes + NumSpritePalettes; i++)
    {
        Sprite s = extractSprite(overlayImage,
                                 x,
                                 y,
                                 width,
                                 height,
                                 mPalettes[i],
                                 mBackgroundColor,
                                 false);
        if(s.colors.size() > bestMaxColors)
            bestIndex = i;
    }
    // Do final extraction with (potential) pixel removal
    Sprite s = extractSprite(overlayImage,
                             x,
                             y,
                             width,
                             height,
                             mPalettes[bestIndex],
                             mBackgroundColor,
                             removePixels);
    s.p = bestIndex;
    return s;
}

//---------------------------------------------------------------------------------------------------------------------

std::vector<Sprite> OverlayOptimiser::spritesOverlayGrid() const
{
    const GridLayer& layer = mLayerOverlay;
    const Array2D<uint8_t>& paletteIndicesOverlay = mPaletteIndicesOverlay;
    std::vector<Sprite> sprites;
    Image2D overlayImage = mOutputImageOverlayGrid;
    for(size_t y = 0; y < layer.height(); y++)
    {
        for(size_t x = 0; x < layer.width(); x++)
        {
            if(layer(x, y).colors.size() > 0)
            {
                uint8_t p = paletteIndicesOverlay(x, y);
                Sprite s = extractSprite(overlayImage,
                                         x * layer.cellWidth(),
                                         y * layer.cellHeight(),
                                         spriteWidth(),
                                         spriteHeight(),
                                         mPalettes[p],
                                         mBackgroundColor,
                                         false);
                s.p = p;
                sprites.push_back(s);
            }
        }
    }
    return sprites;
}

//---------------------------------------------------------------------------------------------------------------------

std::vector<Sprite> OverlayOptimiser::spritesOverlayFree() const
{
    Image2D overlayImage = mOutputImageOverlayFree;
    std::vector<Sprite> sprites;
    size_t y = 0;
    while(y < overlayImage.height())
    {
        // Find first non-empty line
        while(y < overlayImage.height() && overlayImage.emptyRow(y, mBackgroundColor))
            y++;
        // Start extracting sprites from this line
        for(size_t x = 0; x < overlayImage.width();)
        {
            bool columnHasPixels = false;
            for(size_t i = y; i < y + spriteHeight(); i++)
            {
                if(i < overlayImage.height() && overlayImage(x, i) != mBackgroundColor)
                {
                    columnHasPixels = true;
                    break;
                }
            }
            if(columnHasPixels)
            {
                // Extract pixels into sprite at (x, y)
                Sprite s = extractSpriteWithBestPalette(overlayImage, x, y, spriteWidth(), spriteHeight(), true);
                if(s.colors.size() > 0)
                {
                    sprites.push_back(s);
                }
                else
                {
                    // No more sprites can be extracted - move along
                    x++;
                }
            }
            else
            {
                // Empty column - move along
                x++;
            }
        }
        // Advance a full sprite height at right end, as all pixels should now have been extracted into sprites
        y += spriteHeight();
    }
    return sprites;
}

//---------------------------------------------------------------------------------------------------------------------

std::vector<Sprite> OverlayOptimiser::spritesOverlay() const
{
    std::vector<Sprite> sprites = spritesOverlayGrid();
    std::vector<Sprite> spritesFree = spritesOverlayFree();
    sprites.insert( sprites.end(), spritesFree.begin(), spritesFree.end() );
    for(Sprite& s : sprites)
    {
        s.numBlankPixelsLeft = getNumBlankPixelsLeft(s);
        s.numBlankPixelsRight = getNumBlankPixelsRight(s);
    }
    return optimizeHorizontallyAdjacentSprites(sprites);
}

//---------------------------------------------------------------------------------------------------------------------

int OverlayOptimiser::getNumBlankPixelsLeft(Sprite sprite) const
{
    assert(sprite.pixels.width() == spriteWidth());
    assert(sprite.pixels.height() == spriteHeight());
    for(int x=0; x < spriteWidth(); x++)
    {
        for(int y=0; y < spriteHeight(); y++)
        {
            if(sprite.pixels(x, y) != mBackgroundColor)
                return x;
        }
    }
    return spriteWidth();
}

//---------------------------------------------------------------------------------------------------------------------

int OverlayOptimiser::getNumBlankPixelsRight(Sprite sprite) const
{
    assert(sprite.pixels.width() == spriteWidth());
    assert(sprite.pixels.height() == spriteHeight());
    for(int x=0; x < spriteWidth(); x++)
    {
        for(int y=0; y < spriteHeight(); y++)
        {
            if(sprite.pixels(spriteWidth() - 1 - x, y) != mBackgroundColor)
                return x;
        }
    }
    return spriteWidth();
}

//---------------------------------------------------------------------------------------------------------------------

std::vector<std::vector<Sprite>> OverlayOptimiser::getAdjacentSlices(std::vector<Sprite> sprites) const
{
    std::vector<std::vector<Sprite>> slices;
    while(sprites.size() > 0)
    {
        bool sliceFound = false;
        for(size_t i = 1; i < sprites.size(); i++)
        {
            const Sprite& previous = sprites[i-1];
            if(sprites[i].x != previous.x + spriteWidth() ||
               sprites[i].y != previous.y ||
               sprites[i].p != previous.p)
            {
                std::vector<Sprite> slice;
                slice.insert(slice.end(), sprites.begin(), sprites.begin() + i);
                sprites.erase(sprites.begin(), sprites.begin() + i);
                slices.push_back(slice);
                sliceFound = true;
                break;
            }
        }
        if(!sliceFound)
        {
            slices.push_back(sprites);
            break;
        }
    }
    return slices;
}

//---------------------------------------------------------------------------------------------------------------------

std::vector<Sprite> OverlayOptimiser::optimizeHorizontallyAdjacentSprites(const std::vector<Sprite>& sprites) const
{
    std::vector<std::vector<Sprite>> adjacentSlices = getAdjacentSlices(sprites);
    std::vector<Sprite> newSprites;
    for(auto& adjacentSlice : adjacentSlices)
    {
        for(size_t firstIndex = 0; firstIndex < adjacentSlice.size(); firstIndex++)
        {
            for(size_t lastIndex = firstIndex + 1; lastIndex < adjacentSlice.size(); lastIndex++)
            {
                Sprite& leftMostSprite = adjacentSlice[firstIndex];
                Sprite& rightMostSprite = adjacentSlice[lastIndex];
                if(leftMostSprite.numBlankPixelsLeft + rightMostSprite.numBlankPixelsRight >= spriteWidth())
                {
                    // Move entire range right by number of pixels given by left_padding
                    for(size_t i = firstIndex; i < lastIndex; i++)
                    {
                        adjacentSlice[i].x += leftMostSprite.numBlankPixelsLeft;
                    }
                    adjacentSlice.erase(adjacentSlice.begin() + lastIndex);
                    // Skip past processed sprites
                    firstIndex = lastIndex;
                }
            }
        }
        newSprites.insert(newSprites.end(), adjacentSlice.begin(), adjacentSlice.end());
    }
    return newSprites;
}

//---------------------------------------------------------------------------------------------------------------------

int OverlayOptimiser::getMaxSpritesPerScanline(const std::vector<Sprite>& sprites) const
{
    std::vector<int> numSpritesPerScanline;
    const Image2D image = outputImage();
    const size_t spriteHeight = mLayerOverlay.cellHeight();
    numSpritesPerScanline.resize(image.height(), 0);
    for(auto const& s : sprites)
    {
        for(size_t y = s.y; y < s.y + spriteHeight; y++)
        {
            if( y < image.height())
            {
                numSpritesPerScanline[y] += 1;
            }
        }
    }
    return *std::max_element(numSpritesPerScanline.begin(), numSpritesPerScanline.end());
}

//---------------------------------------------------------------------------------------------------------------------

uint8_t OverlayOptimiser::indexInPalette(const std::set<uint8_t>& palette, uint8_t color)
{
    uint8_t i = 1;
    for(uint8_t c : palette)
    {
        if(c == color)
            return i;
        i++;
    }
    return 0;
}

//---------------------------------------------------------------------------------------------------------------------

int OverlayOptimiser::spriteWidth() const
{
    return SpriteWidth;
}

//---------------------------------------------------------------------------------------------------------------------

int OverlayOptimiser::spriteHeight() const
{
    return mSpriteHeight;
}

//---------------------------------------------------------------------------------------------------------------------

uint8_t OverlayOptimiser::backgroundColor() const
{
    return mBackgroundColor;
}
