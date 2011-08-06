/**
 * @file
 * @brief Group belonging to an SVG drawing element
 *//*
 * Authors:
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *
 * Copyright (C) 2011 Authors
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifndef SEEN_INKSCAPE_DISPLAY_DRAWING_SHAPE_H
#define SEEN_INKSCAPE_DISPLAY_DRAWING_SHAPE_H

#include "display/drawing-item.h"
#include "display/nr-style.h"

class SPStyle;
class SPCurve;

namespace Inkscape {

class DrawingShape
    : public DrawingItem
{
public:
    DrawingShape(Drawing *drawing);
    ~DrawingShape();

    void setPath(SPCurve *curve);
    void setStyle(SPStyle *style);
    void setPaintBox(Geom::Rect const &box);

protected:
    unsigned _updateItem(Geom::IntRect const &area, UpdateContext const &ctx,
                                 unsigned flags, unsigned reset);
    virtual void _renderItem(DrawingContext &ct, Geom::IntRect const &area, unsigned flags);
    virtual void _clipItem(DrawingContext &ct, Geom::IntRect const &area);
    virtual DrawingItem *_pickItem(Geom::Point const &p, double delta);
    virtual bool _canClip();

    SPCurve *_curve;
    SPStyle *_style;
    NRStyle _nrstyle;

    Geom::OptRect _paintbox;
    DrawingItem *_last_pick;
    unsigned _repick_after;
};

} // end namespace Inkscape

#endif // !SEEN_INKSCAPE_DISPLAY_DRAWING_ITEM_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
