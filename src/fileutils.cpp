/****************************************************************************
Copyright 2015 Ricardo Quesada

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
****************************************************************************/

#include "fileutils.h"

#include <QFileInfo>
#include <QDir>

QString FileUtils::getShortNativePath(const QString& filename)
{
#ifdef Q_OS_UNIX
    auto filepath = QFileInfo(filename).canonicalFilePath();
    auto homepath = QDir::cleanPath(QDir::homePath());
    if (filepath.startsWith(homepath))
    {
        filepath.remove(homepath);
        return QLatin1Char('~') + QDir::toNativeSeparators(filepath);
    }
    return QDir::toNativeSeparators(QDir::cleanPath(filepath));
#else
    return QDir::toNativeSeparators(QDir::cleanPath(filename));
#endif

}

