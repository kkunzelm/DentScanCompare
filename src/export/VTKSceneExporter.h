#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>


#include <QString>
#include <vtkSmartPointer.h>
#include <vtkRenderWindow.h>

namespace VTKSceneExporter {

// Render a VTK window offscreen at the given pixel size and save as PNG.
// Useful for publication-quality figures without needing to resize the UI.
bool renderOffscreen(vtkRenderWindow* window,
                     int width, int height,
                     const QString& filePath);

} // namespace VTKSceneExporter
