// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>

#include "VTKSceneExporter.h"
#include "ReportExporter.h"

#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>

namespace VTKSceneExporter {

bool renderOffscreen(vtkRenderWindow* window,
                     int width, int height,
                     const QString& filePath)
{
    if (!window) return false;

    int oldSize[2] = { window->GetSize()[0], window->GetSize()[1] };
    window->SetSize(width, height);
    window->Render();

    bool ok = ReportExporter::toPNG(window, filePath, 1);

    window->SetSize(oldSize[0], oldSize[1]);
    window->Render();
    return ok;
}

} // namespace VTKSceneExporter
