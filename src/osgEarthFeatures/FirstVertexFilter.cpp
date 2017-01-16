#include <osgEarthFeatures/FirstVertexFilter>

#define LC "[FirstVertexFilter] "

using namespace osgEarth;
using namespace osgEarth::Features;

//------------------------------------------------------------------------

FirstVertexFilter::FirstVertexFilter()
{
    //NOP
}

FilterContext
FirstVertexFilter::push(FeatureList& features, FilterContext& context )
{
    for( FeatureList::iterator i = features.begin(); i != features.end(); ++i )
    {
        Feature* f = i->get();
        
        Geometry* geom = f->getGeometry();
        if ( !geom )
            continue;

        if ( geom )
        {
            switch( geom->getComponentType() )
            {
            case Geometry::TYPE_LINESTRING:
                osgEarth::Symbology::LineString* line = (LineString*) geom;

                PointSet* newGeom = new PointSet();
                osg::Vec3dArray* arr = line->createVec3dArray();

                newGeom->push_back(arr->at(1));
                f->setGeometry( newGeom );
                break;
              }
        }
    }

    return context;
}
