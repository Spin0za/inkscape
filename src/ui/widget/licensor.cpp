/** \file
 *
 * Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon Phillips <jon@rejon.org>
 *   Ralf Stephan <ralf@ark.in-berlin.de> (Gtkmm)
 *
 * Copyright (C) 2000 - 2005 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gtkmm/entry.h>

#include "ui/widget/entity-entry.h"
#include "ui/widget/registry.h"
#include "dialogs/rdf.h"
#include "inkscape.h"

#include "licensor.h"

namespace Inkscape {
namespace UI {
namespace Widget {

//===================================================

const struct rdf_license_t _proprietary_license = 
  {_("Proprietary"), "", 0};

class LicenseItem : public Gtk::RadioButton {
public:
    LicenseItem (struct rdf_license_t const* license, EntityEntry* entity, Registry &wr);
protected:
    void on_toggled();
    struct rdf_license_t const *_lic;
    EntityEntry                *_eep;
    Registry                   &_wr;
};

LicenseItem::LicenseItem (struct rdf_license_t const* license, EntityEntry* entity, Registry &wr)
: Gtk::RadioButton(_(license->name)), _lic(license), _eep(entity), _wr(wr)
{
    static Gtk::RadioButtonGroup group = get_group();
    static bool first = true;
    if (first) first = false;
    else       set_group (group);
}

/// \pre it is assumed that the license URI entry is a Gtk::Entry
void
LicenseItem::on_toggled()
{
    if (_wr.isUpdating()) return;

    _wr.setUpdating (true);
    rdf_set_license (SP_ACTIVE_DOCUMENT, _lic->details ? _lic : 0);
    sp_document_done (SP_ACTIVE_DOCUMENT, SP_VERB_NONE, 
                      /* TODO: annotate */ "licensor.cpp:65");
    _wr.setUpdating (false);
    reinterpret_cast<Gtk::Entry*>(_eep->_packable)->set_text (_lic->uri);
    _eep->on_changed();
}

//---------------------------------------------------

Licensor::Licensor()
: Gtk::VBox(false,4)
{
}

Licensor::~Licensor()
{
    if (_eentry) delete _eentry;
}

void
Licensor::init (Gtk::Tooltips& tt, Registry& wr)
{
    /* add license-specific metadata entry areas */
    rdf_work_entity_t* entity = rdf_find_entity ( "license_uri" );
    _eentry = EntityEntry::create (entity, tt, wr);

    LicenseItem *i;
    wr.setUpdating (true);
    i = manage (new LicenseItem (&_proprietary_license, _eentry, wr));
    add (*i);
    LicenseItem *pd = i;
    for (struct rdf_license_t * license = rdf_licenses;
             license && license->name;
             license++) {
        i = manage (new LicenseItem (license, _eentry, wr));
        add(*i);
    }
    pd->set_active();
    wr.setUpdating (false);

    Gtk::HBox *box = manage (new Gtk::HBox);
    pack_start (*box, true, true, 0);

    box->pack_start (_eentry->_label, false, false, 5);
    box->pack_start (*_eentry->_packable, true, true, 0);

    show_all_children();
}

void 
Licensor::update (SPDocument *doc)
{
    /* identify the license info */
    struct rdf_license_t * license = rdf_get_license (doc);

    if (license) {
        int i;
        for (i=0; rdf_licenses[i].name; i++) 
            if (license == &rdf_licenses[i]) 
                break;
        reinterpret_cast<LicenseItem*>(children()[i+1].get_widget())->set_active();
    }
    else {
        reinterpret_cast<LicenseItem*>(children()[0].get_widget())->set_active();
    }
    
    /* update the URI */
    _eentry->update (doc);
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=c++:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
