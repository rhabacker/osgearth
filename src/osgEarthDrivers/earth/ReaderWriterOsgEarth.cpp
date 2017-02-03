/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
 * Copyright 2016 Pelican Mapping
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
#include "EarthFileSerializer"
#include <osgEarth/Map>
#include <osgEarth/MapNode>
#include <osgEarth/Registry>
#include <osgEarth/XmlUtils>
#include <osgEarth/XmlUtils>
#include <osgEarthSymbology/Color>
#include <osgDB/FileUtils>
#include <osgDB/Registry>
#include <string>
#include <sstream>
#include <osgEarthUtil/Common>
#include <osgEarthUtil/Controls>
#include <osgEarthUtil/EarthManipulator>
#include <osgViewer/Viewer>

using namespace osgEarth::Util::Controls;
using namespace osgEarth::Util;
using namespace osgEarth_osgearth;
using namespace osgEarth;

#define LC "[Earth Plugin] "

// Macros to determine the filename for dependent libs.
#define Q2(x) #x
#define Q(x)  Q2(x)

#if (defined(_DEBUG) || defined(QT_DEBUG)) && defined(OSGEARTH_DEBUG_POSTFIX)
#   define LIBNAME_UTIL_POSTFIX Q(OSGEARTH_DEBUG_POSTFIX)
#elif defined(OSGEARTH_RELEASE_POSTFIX)
#   define LIBNAME_UTIL_POSTFIX Q(OSGEARTH_RELEASE_POSTFIX)
#else
#   define LIBNAME_UTIL_POSTFIX ""
#endif

#if defined(WIN32)
#   define LIBNAME_UTIL "osgEarthUtil"
#   define LIBNAME_UTIL_EXTENSION ".dll"
#else
#   define LIBNAME_UTIL "libosgEarthUtil"
#   if defined(__APPLE__)
#       define LIBNAME_UTIL_EXTENSION ".dylib"
#   else
#       define LIBNAME_UTIL_EXTENSION ".so"
#   endif
#endif


// OSG OPTIONS

// By default the writer will re-write relative pathnames so they are relative to the new
// save location. This option will disable that.
#define EARTH_DO_NOT_REWRITE_PATHS   "DoNotRewritePaths"

// By default the writer will skip absolute paths and leave them as-is. This option will
// cause the writer to try making absolute paths relative to the new save location.
#define EARTH_REWRITE_ABSOLUTE_PATHS "RewriteAbsolutePaths"


/**
 * Toggles the main control canvas on and off.
 */
struct ToggleCanvasEventHandler : public osgGA::GUIEventHandler
{
    ToggleCanvasEventHandler(osg::Node* canvas, char key) :
        _canvas(canvas), _key(key)
    {
    }

    bool handle( const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa )
    {
        if (ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN)
        {
            if (ea.getKey() == _key)
            {
                osg::ref_ptr< osg::Node > safeNode = _canvas.get();
                if (safeNode.valid())
                {
                    safeNode->setNodeMask( safeNode->getNodeMask() ? 0 : ~0 );
                }
                return true;
            }
        }
        return false;
    }

    osg::observer_ptr<osg::Node> _canvas;
    char _key;
};

namespace
{
    void recursiveUniqueKeyMerge(Config& lhs, const Config& rhs)
    {
        if ( rhs.value() != lhs.value() )
        {
            lhs.value() = rhs.value();
        }

        for(ConfigSet::const_iterator rhsChild = rhs.children().begin(); rhsChild != rhs.children().end(); ++rhsChild)
        {
            Config* lhsChild = lhs.mutable_child(rhsChild->key());
            if ( lhsChild )
                recursiveUniqueKeyMerge( *lhsChild, *rhsChild );
            else
                lhs.add( *rhsChild );
        }
    }
}


class ReaderWriterEarth : public osgDB::ReaderWriter
{
    public:
        ReaderWriterEarth()
        {
            // force the loading of other osgEarth libraries that might be needed to 
            // deserialize an earth file. 
            // osgEarthUtil: contains ColorFilter implementations
            OE_DEBUG << LC << "Forced load: " << LIBNAME_UTIL LIBNAME_UTIL_POSTFIX LIBNAME_UTIL_EXTENSION << std::endl;
            osgDB::Registry::instance()->loadLibrary( LIBNAME_UTIL LIBNAME_UTIL_POSTFIX LIBNAME_UTIL_EXTENSION );
        }

        virtual const char* className() const
        {
            return "OSG Earth ReaderWriter";
        }
        
        virtual bool acceptsExtension(const std::string& extension) const
        {
            return osgDB::equalCaseInsensitive( extension, "earth" );
        }

        virtual ReadResult readObject(const std::string& file_name, const osgDB::Options* options) const
        {
            return readNode( file_name, options );
        }

        virtual ReadResult readObject(std::istream& in, const osgDB::Options* options) const
        {
            return readNode( in, options );
        }

        virtual WriteResult writeNode(const osg::Node& node, const std::string& fileName, const osgDB::Options* options ) const
        {
            if ( !acceptsExtension( osgDB::getFileExtension(fileName) ) )
                return WriteResult::FILE_NOT_HANDLED;

            std::ofstream out( fileName.c_str());
            if ( out.is_open() )
            {
                osg::ref_ptr<osgDB::Options> myOptions = Registry::instance()->cloneOrCreateOptions(options);
                URIContext( fileName ).store( myOptions.get() );

                return writeNode( node, out, myOptions.get() );
            }

            return WriteResult::ERROR_IN_WRITING_FILE;            
        }

        virtual WriteResult writeNode(const osg::Node& node, std::ostream& out, const osgDB::Options* options ) const
        {
            osg::Node* searchNode = const_cast<osg::Node*>( &node );
            MapNode* mapNode = MapNode::findMapNode( searchNode );
            if ( !mapNode )
                return WriteResult::ERROR_IN_WRITING_FILE; // i.e., no MapNode found in the graph.
            
            // decode the context from the options (might be there, might not)
            URIContext uriContext( options );

            // serialize the map node to a generic Config object:
            EarthFileSerializer2 ser;

            // check for writer options
            if ( options )
            {
                std::string ostr = osgEarth::toLower(options->getOptionString());

                if ( ostr.find(toLower(EARTH_DO_NOT_REWRITE_PATHS)) != std::string::npos )
                {
                    OE_INFO << LC << "path re-writing disabled\n";
                    ser.setRewritePaths( false );
                }

                if ( ostr.find(toLower(EARTH_REWRITE_ABSOLUTE_PATHS)) != std::string::npos )
                {
                    OE_INFO << LC << "absolute path re-writing enabled\n";
                    ser.setRewriteAbsolutePaths( true );
                }
            }

            Config conf = ser.serialize( mapNode, uriContext.referrer() );

            // dump that Config out as XML.
            osg::ref_ptr<XmlDocument> xml = new XmlDocument( conf );
            xml->store( out );

            return WriteResult::FILE_SAVED;
        }

        virtual ReadResult readNode(const std::string& fileName, const osgDB::Options* readOptions) const
        {
            std::string ext = osgDB::getFileExtension( fileName );
            if ( !acceptsExtension( ext ) )
                return ReadResult::FILE_NOT_HANDLED;

            // See if the filename starts with server: and strip it off.  This will trick OSG into
            // passing in the filename to our plugin instead of using the CURL plugin if the filename
            // contains a URL.  So, if you want to read a URL, you can use the following format
            // osgDB::readNodeFile("server:http://myserver/myearth.earth").  This should only be
            // necessary for the first level as the other files will have a tilekey prepended to them.
            if ((fileName.length() > 7) && (fileName.substr(0, 7) == "server:"))
                return readNode(fileName.substr(7), readOptions);            

            if ( fileName == "__globe.earth" )
            {
                return ReadResult( new MapNode() );
            }

            else
            {
                std::string fullFileName = fileName;
                if ( !osgDB::containsServerAddress( fileName ) )
                {
                    fullFileName = osgDB::findDataFile( fileName, readOptions );
                    if (fullFileName.empty()) return ReadResult::FILE_NOT_FOUND;
                }

                osgEarth::ReadResult r = URI(fullFileName).readString( readOptions );
                if ( r.failed() )
                    return ReadResult::ERROR_IN_READING_FILE;

                // Since we're now passing off control to the stream, we have to pass along the
                // reference URI as well..
                osg::ref_ptr<osgDB::Options> myReadOptions = Registry::instance()->cloneOrCreateOptions(readOptions);

                URIContext( fullFileName ).store( myReadOptions.get() );

                std::stringstream in( r.getString() );
                return readNode( in, myReadOptions.get() );
            }
        }

        virtual ReadResult readNode(std::istream& in, const osgDB::Options* readOptions) const
        {
            // pull the URI context from the options structure (since we're reading
            // from an "anonymous" stream here)
            URIContext uriContext( readOptions ); 

            osg::ref_ptr<XmlDocument> doc = XmlDocument::load( in, uriContext );
            if ( !doc.valid() )
                return ReadResult::ERROR_IN_READING_FILE;

            Config docConf = doc->getConfig();

            // support both "map" and "earth" tag names at the top level
            Config conf;
            if ( docConf.hasChild( "map" ) )
                conf = docConf.child( "map" );
            else if ( docConf.hasChild( "earth" ) )
                conf = docConf.child( "earth" );

            osg::ref_ptr<osg::Node> node;

            if ( !conf.empty() )
            {
                // see if we were given a reference URI to use:
                std::string refURI = uriContext.referrer();

                if ( conf.value("version") == "1" )
                {
                    OE_INFO << LC << "Detected a version 1.x earth file" << std::endl;
                    EarthFileSerializer1 ser;
                    node = ser.deserialize( conf, refURI );
                }

                else
                {
                    if ( conf.value("version") != "2" )
                        OE_DEBUG << LC << "No valid earth file version; assuming version='2'" << std::endl;

                    // attempt to parse a "default options" JSON string:
                    std::string defaultConfStr;
                    if ( readOptions )
                    {
                        defaultConfStr = readOptions->getPluginStringData("osgEarth.defaultOptions");
                        if ( !defaultConfStr.empty() )
                        {
                            Config optionsConf("options");
                            if (optionsConf.fromJSON(defaultConfStr))
                            {
                                //OE_NOTICE << "\n\nOriginal = \n" << conf.toJSON(true) << "\n";
                                Config* original = conf.mutable_child("options");
                                if ( original )
                                {
                                    recursiveUniqueKeyMerge(optionsConf, *original);
                                }
                                if ( !optionsConf.empty() )
                                {
                                    conf.set("options", optionsConf);
                                }
                                //OE_NOTICE << "\n\nMerged = \n" << conf.toJSON(true) << "\n";
                            }
                        }
                    }

                    EarthFileSerializer2 ser;
                    node = ser.deserialize( conf, refURI );
                }
            }

            MapNode* mapNode = MapNode::get(node.get());
            if (mapNode)
            {
                // If the user passed in a cache object, apply it to the map now
                CacheSettings* cacheSettings = CacheSettings::get(readOptions);
                if (cacheSettings && cacheSettings->getCache())
                {
                    mapNode->getMap()->setCache( cacheSettings->getCache() );
                    OE_INFO << LC << "Applied user-supplied cache to the Map\n";
                }
            }

            osgViewer::Viewer *view = dynamic_cast<osgViewer::Viewer*>(osgDB::Registry::instance()->getFromObjectCache("__viewer__"));
            if (view)
            {
                // Install a new Canvas for our UI controls, or use one that already exists.
                ControlCanvas* canvas = ControlCanvas::getOrCreate( view );

                Container* mainContainer;
                mainContainer = new VBox();
                mainContainer->setAbsorbEvents( true );
                mainContainer->setBackColor( Color(Color::Black, 0.8) );
                mainContainer->setHorizAlign( Control::ALIGN_LEFT );
                mainContainer->setVertAlign( Control::ALIGN_BOTTOM );
                canvas->addControl( mainContainer );

                // Add an event handler to toggle the canvas with a key press;
                view->addEventHandler(new ToggleCanvasEventHandler(canvas, 'y'));
                // install our default manipulator (do this before calling load)
                view->setCameraManipulator( new EarthManipulator );

                // Hook up the extensions!
                for(std::vector<osg::ref_ptr<Extension> >::const_iterator eiter = mapNode->getExtensions().begin();
                    eiter != mapNode->getExtensions().end();
                    ++eiter)
                {
                    Extension* e = eiter->get();

                    // Check for a View interface:
                    ExtensionInterface<osg::View>* viewIF = ExtensionInterface<osg::View>::get( e );
                    if ( viewIF )
                        viewIF->connect( view );

                    // Check for a Control interface:
                    ExtensionInterface<Control>* controlIF = ExtensionInterface<Control>::get( e );
                    if ( controlIF )
                        controlIF->connect( mainContainer );
                }
            }

            return ReadResult(node.get());
        }
};

REGISTER_OSGPLUGIN(earth, ReaderWriterEarth)
