/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2014 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include "UniformCache"
#include <osgEarth/Notify>

using namespace osgEarth::Drivers::RexTerrainEngine;

osg::Uniform* UniformCache::getOrCreateMatrixUniform(const std::string& name, const osg::Matrixf& m)
{
    typedef std::map<osg::Vec4f, osg::ref_ptr<osg::Uniform> > MatrixUniformMap;
    static OpenThreads::Mutex s_mutex_matrixUniforms;
    static MatrixUniformMap s_matrixUniforms;
    
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(s_mutex_matrixUniforms);

    // Unique key for this uniform include the scale, the x/y bias, and the name ID.
    osg::Vec4f key(m(0,0),m(3,0),m(3,1),(float)osg::Uniform::getNameID(name));

    MatrixUniformMap::iterator i = s_matrixUniforms.find(key);
    if ( i != s_matrixUniforms.end() )
    {
        return i->second.get();
    }

    osg::Uniform* u = new osg::Uniform(name.c_str(), m);
    s_matrixUniforms[key] = u;

    return u;
}
