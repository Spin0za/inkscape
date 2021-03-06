/**
 * @file
 * Symbols dialog.
 */
/* Authors:
 * Copyright (C) 2012 Tavmjong Bah
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <iostream>
#include <algorithm>

#include <gtkmm/buttonbox.h>
#include <gtkmm/label.h>
#include <gtkmm/table.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/iconview.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treemodelcolumn.h>
#include <gtkmm/clipboard.h>

#include "path-prefix.h"
#include "io/sys.h"

#include "ui/cache/svg_preview_cache.h"
#include "ui/clipboard.h"

#include "symbols.h"

#include "desktop.h"
#include "desktop-handles.h"
#include "document.h"
#include "inkscape.h"
#include "sp-root.h"
#include "sp-use.h"
#include "sp-symbol.h"

#include "verbs.h"
#include "xml/repr.h"

namespace Inkscape {
namespace UI {

static Cache::SvgPreview svg_preview_cache;

namespace Dialog {

  // See: http://developer.gnome.org/gtkmm/stable/classGtk_1_1TreeModelColumnRecord.html
class SymbolColumns : public Gtk::TreeModel::ColumnRecord
{
public:

  Gtk::TreeModelColumn<Glib::ustring>                symbol_id;
  Gtk::TreeModelColumn<Glib::ustring>                symbol_title;
  Gtk::TreeModelColumn< Glib::RefPtr<Gdk::Pixbuf> >  symbol_image;

  SymbolColumns() {
    add(symbol_id);
    add(symbol_title);
    add(symbol_image);
  }
};

SymbolColumns* SymbolsDialog::getColumns()
{
  SymbolColumns* columns = new SymbolColumns();
  return columns;
}

/**
 * Constructor
 */
SymbolsDialog::SymbolsDialog( gchar const* prefsPath ) :
  UI::Widget::Panel("", prefsPath, SP_VERB_DIALOG_SYMBOLS),
  store(Gtk::ListStore::create(*getColumns())),
  iconView(0),
  previewScale(0),
  previewSize(0),
  currentDesktop(0),
  deskTrack(),
  currentDocument(0),
  previewDocument(0),
  instanceConns()
{

  /********************    Table    *************************/
  // Replace by Grid for GTK 3.0
  Gtk::Table *table = new Gtk::Table(2, 4, false);
  // panel is a cloked Gtk::VBox
  _getContents()->pack_start(*Gtk::manage(table), Gtk::PACK_EXPAND_WIDGET);
  guint row = 0;

  /******************** Symbol Sets *************************/
  Gtk::Label* labelSet = new Gtk::Label("Symbol set: ");
  table->attach(*Gtk::manage(labelSet),0,1,row,row+1,Gtk::SHRINK,Gtk::SHRINK);

  symbolSet = new Gtk::ComboBoxText();  // Fill in later
#if WITH_GTKMM_2_24
  symbolSet->append("Current Document");
#else
  symbolSet->append_text("Current Document");
#endif
  symbolSet->set_active_text("Current Document");
  table->attach(*Gtk::manage(symbolSet),1,2,row,row+1,Gtk::FILL|Gtk::EXPAND,Gtk::SHRINK);

  sigc::connection connSet =
    symbolSet->signal_changed().connect(sigc::mem_fun(*this, &SymbolsDialog::rebuild));
  instanceConns.push_back(connSet);
  
  ++row;

  /********************* Icon View **************************/
  SymbolColumns* columns = getColumns();

  iconView = new Gtk::IconView(static_cast<Glib::RefPtr<Gtk::TreeModel> >(store));
  //iconView->set_text_column(  columns->symbol_id  );
  iconView->set_tooltip_column( 1 );
  iconView->set_pixbuf_column(  columns->symbol_image );

  sigc::connection connIconChanged;
  connIconChanged =
    iconView->signal_selection_changed().connect(sigc::mem_fun(*this, &SymbolsDialog::iconChanged));
  instanceConns.push_back(connIconChanged);

  Gtk::ScrolledWindow *scroller = new Gtk::ScrolledWindow();
  scroller->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_ALWAYS);
  scroller->add(*Gtk::manage(iconView));
  table->attach(*Gtk::manage(scroller),0,2,row,row+1,Gtk::EXPAND|Gtk::FILL,Gtk::EXPAND|Gtk::FILL);

  ++row;

  /******************** Preview Scale ***********************/
  Gtk::Label* labelScale = new Gtk::Label("Preview scale: ");
  table->attach(*Gtk::manage(labelScale),0,1,row,row+1,Gtk::SHRINK,Gtk::SHRINK);

  previewScale = new Gtk::ComboBoxText();
  const gchar *scales[] =
    {"Fit", "Fit to width", "Fit to height", "0.1", "0.2", "0.5", "1.0", "2.0", "5.0", NULL};
  for( int i = 0; scales[i]; ++i ) {
#if WITH_GTKMM_2_24
    previewScale->append(scales[i]);
#else
    previewScale->append_text(scales[i]);
#endif
  }
  previewScale->set_active_text(scales[0]);
  table->attach(*Gtk::manage(previewScale),1,2,row,row+1,Gtk::FILL|Gtk::EXPAND,Gtk::SHRINK);

  sigc::connection connScale =
    previewScale->signal_changed().connect(sigc::mem_fun(*this, &SymbolsDialog::rebuild));
  instanceConns.push_back(connScale);
  
  ++row;

  /******************** Preview Size ************************/
  Gtk::Label* labelSize = new Gtk::Label("Preview size: ");
  table->attach(*Gtk::manage(labelSize),0,1,row,row+1,Gtk::SHRINK,Gtk::SHRINK);

  previewSize = new Gtk::ComboBoxText();
  const gchar *sizes[] = {"16", "24", "32", "48", "64", NULL};
  for( int i = 0; sizes[i]; ++i ) {
#if WITH_GTKMM_2_24
    previewSize->append(sizes[i]);
#else
    previewSize->append_text(sizes[i]);
#endif

  }
  previewSize->set_active_text(sizes[2]);
  table->attach(*Gtk::manage(previewSize),1,2,row,row+1,Gtk::FILL|Gtk::EXPAND,Gtk::SHRINK);

  sigc::connection connSize =
    previewSize->signal_changed().connect(sigc::mem_fun(*this, &SymbolsDialog::rebuild));
  instanceConns.push_back(connSize);
  
  ++row;

  /**********************************************************/
  currentDesktop  = inkscape_active_desktop();
  currentDocument = sp_desktop_document(currentDesktop);

  previewDocument = symbols_preview_doc(); /* Template to render symbols in */
  previewDocument->ensureUpToDate(); /* Necessary? */

  key = SPItem::display_key_new(1);
  renderDrawing.setRoot(previewDocument->getRoot()->invoke_show(renderDrawing, key, SP_ITEM_SHOW_DISPLAY ));

  get_symbols();
  draw_symbols( currentDocument ); /* Defaults to current document */

  desktopChangeConn =
    deskTrack.connectDesktopChanged( sigc::mem_fun(*this, &SymbolsDialog::setTargetDesktop) );
  instanceConns.push_back( desktopChangeConn );
  deskTrack.connect(GTK_WIDGET(gobj()));
}

SymbolsDialog::~SymbolsDialog()
{
  for (std::vector<sigc::connection>::iterator it =  instanceConns.begin(); it != instanceConns.end(); ++it) {
      it->disconnect();
  }
  instanceConns.clear();
  deskTrack.disconnect();
}

SymbolsDialog& SymbolsDialog::getInstance()
{
  return *new SymbolsDialog();
}

void SymbolsDialog::rebuild() {

  store->clear();
  Glib::ustring symbolSetString = symbolSet->get_active_text();

  SPDocument* symbolDocument = symbolSets[symbolSetString];
  if( !symbolDocument ) {
    // Symbol must be from Current Document (this method of
    // checking should be language independent).
    symbolDocument = currentDocument;
  }
  draw_symbols( symbolDocument );
}

void SymbolsDialog::iconChanged() {
#if WITH_GTKMM_3_0
  std::vector<Gtk::TreePath> iconArray = iconView->get_selected_items();
#else
  Gtk::IconView::ArrayHandle_TreePaths iconArray = iconView->get_selected_items();
#endif

  if( iconArray.empty() ) {
    //std::cout << "  iconArray empty: huh? " << std::endl;
  } else {
    Gtk::TreeModel::Path const & path = *iconArray.begin();
    Gtk::ListStore::iterator row = store->get_iter(path);
    Glib::ustring symbol_id = (*row)[getColumns()->symbol_id];

    /* OK, we know symbol name... now we need to copy it to clipboard, bon chance! */
    Glib::ustring symbolSetString = symbolSet->get_active_text();

    SPDocument* symbolDocument = symbolSets[symbolSetString];
    if( !symbolDocument ) {
      // Symbol must be from Current Document (this method of
      // checking should be language independent).
      symbolDocument = currentDocument;
    }

    SPObject* symbol = symbolDocument->getObjectById(symbol_id);
    if( symbol ) {

      // Find style for use in <use>
      // First look for default style stored in <symbol>
      gchar const* style = symbol->getAttribute("inkscape:symbol-style");
      if( !style ) {
	// If no default style in <symbol>, look in documents.
	if( symbolDocument == currentDocument ) {
	  style = style_from_use( symbol_id.c_str(), currentDocument );
	} else {
	  style = symbolDocument->getReprRoot()->attribute("style");
	}
      }

      ClipboardManager *cm = ClipboardManager::get();
      cm->copySymbol(symbol->getRepr(), style);
    }
  }
}

/* Hunts preference directories for symbol files */
void SymbolsDialog::get_symbols() {

  std::list<Glib::ustring> directories;

  if( Inkscape::IO::file_test( INKSCAPE_SYMBOLSDIR, G_FILE_TEST_EXISTS ) &&
	Inkscape::IO::file_test( INKSCAPE_SYMBOLSDIR, G_FILE_TEST_IS_DIR ) ) {
    directories.push_back( INKSCAPE_SYMBOLSDIR );
  }
  if( Inkscape::IO::file_test( profile_path("symbols"), G_FILE_TEST_EXISTS ) &&
	Inkscape::IO::file_test( profile_path("symbols"), G_FILE_TEST_IS_DIR ) ) {
    directories.push_back( profile_path("symbols") );
  }

  std::list<Glib::ustring>::iterator it;
  for( it = directories.begin(); it != directories.end(); ++it ) {

    GError *err = 0;
    GDir *dir = g_dir_open( (*it).c_str(), 0, &err );
    if( dir ) {

	gchar *filename = 0;
	while( (filename = (gchar *)g_dir_read_name( dir ) ) != NULL) {

	  gchar *fullname = g_build_filename((*it).c_str(), filename, NULL);

	  if ( !Inkscape::IO::file_test( fullname, G_FILE_TEST_IS_DIR ) ) {

	    SPDocument* symbol_doc = SPDocument::createNewDoc( fullname, FALSE );
	    if( symbol_doc ) {
	      symbolSets[Glib::ustring(filename)]= symbol_doc;
#if WITH_GTKMM_2_24
	      symbolSet->append(filename);
#else
	      symbolSet->append_text(filename);
#endif
	    }
	  }
	  g_free( fullname );
	}
	g_dir_close( dir );
    }
  }
}

GSList* SymbolsDialog::symbols_in_doc_recursive (SPObject *r, GSList *l)
{ 

  // Stop multiple counting of same symbol
  if( SP_IS_USE(r) ) {
    return l;
  }

  if( SP_IS_SYMBOL(r) ) {
    l = g_slist_prepend (l, r);
  }

  for (SPObject *child = r->firstChild(); child; child = child->getNext()) {
    l = symbols_in_doc_recursive( child, l );
  }

  return l;
}

GSList* SymbolsDialog::symbols_in_doc( SPDocument* symbolDocument ) {

  GSList *l = NULL;
  l = symbols_in_doc_recursive (symbolDocument->getRoot(), l );
  return l;
}

GSList* SymbolsDialog::use_in_doc_recursive (SPObject *r, GSList *l)
{ 

  if( SP_IS_USE(r) ) {
    l = g_slist_prepend (l, r);
  }

  for (SPObject *child = r->firstChild(); child; child = child->getNext()) {
    l = use_in_doc_recursive( child, l );
  }

  return l;
}

GSList* SymbolsDialog::use_in_doc( SPDocument* useDocument ) {

  GSList *l = NULL;
  l = use_in_doc_recursive (useDocument->getRoot(), l );
  return l;
}

// Returns style from first <use> element found that references id.
// This is a last ditch effort to find a style.
gchar const* SymbolsDialog::style_from_use( gchar const* id, SPDocument* document) {

  gchar const* style = 0;
  GSList* l = use_in_doc( document );
  for( ; l != NULL; l = l->next ) {
    SPObject* use = SP_OBJECT(l->data);
    if( SP_IS_USE( use ) ) {
	gchar const *href = use->getRepr()->attribute("xlink:href");
	if( href ) {
	  Glib::ustring href2(href);
	  Glib::ustring id2(id);
	  id2 = "#" + id2;
	  if( !href2.compare(id2) ) {
	    style = use->getRepr()->attribute("style");
	    break;
	  }
	}
    }
  }
  return style;
}

void SymbolsDialog::draw_symbols( SPDocument* symbolDocument ) {


  SymbolColumns* columns = getColumns();

  GSList* l = symbols_in_doc( symbolDocument );
  for( ; l != NULL; l = l->next ) {

    SPObject* symbol = SP_OBJECT(l->data);
    if (!SP_IS_SYMBOL(symbol)) {
	//std::cout << "  Error: not symbol" << std::endl;
	continue;
    }

    gchar const *id    = symbol->getRepr()->attribute("id");
    gchar const *title = symbol->title(); // From title element
    if( !title ) {
	title = id;
    }

    Glib::RefPtr<Gdk::Pixbuf> pixbuf = create_symbol_image(id, symbolDocument, &renderDrawing, key );
    if( pixbuf ) {

	Gtk::ListStore::iterator row = store->append();
	(*row)[columns->symbol_id]    = Glib::ustring( id );
	(*row)[columns->symbol_title] = Glib::ustring( title );
	(*row)[columns->symbol_image] = pixbuf;
    }
  }

  delete columns;
}

/*
 * Returns image of symbol.
 *
 * Symbols normally are not visible. They must be referenced by a
 * <use> element.  A temporary document is created with a dummy
 * <symbol> element and a <use> element that references the symbol
 * element. Each real symbol is swapped in for the dummy symbol and
 * the temporary document is rendered.
 */
Glib::RefPtr<Gdk::Pixbuf>
SymbolsDialog::create_symbol_image(gchar const *symbol_id,
				     SPDocument *source, 
				     Inkscape::Drawing* drawing,
				     unsigned /*visionkey*/)
{
  // Retrieve the symbol named 'symbol_id' from the source SVG document
  SPObject const* symbol = source->getObjectById(symbol_id);
  if (symbol == NULL) {
    //std::cout << "  Failed to find symbol: " << symbol_id << std::endl;
    //return 0;
  }

  // Create a copy repr of the symbol with id="the_symbol"
  Inkscape::XML::Document *xml_doc = previewDocument->getReprDoc();
  Inkscape::XML::Node *repr = symbol->getRepr()->duplicate(xml_doc);
  repr->setAttribute("id", "the_symbol");

  // Replace old "the_symbol" in previewDocument by new.
  Inkscape::XML::Node *root = previewDocument->getReprRoot();
  SPObject *symbol_old = previewDocument->getObjectById("the_symbol");
  if (symbol_old) {
      symbol_old->deleteObject(false);
  }

  // First look for default style stored in <symbol>
  gchar const* style = repr->attribute("inkscape:symbol-style");
  if( !style ) {
    // If no default style in <symbol>, look in documents.
    if( source == currentDocument ) {
	style = style_from_use( symbol_id, source );
    } else {
	style = source->getReprRoot()->attribute("style");
    }
  }
  // Last ditch effort to provide some default styling
  if( !style ) style = "fill:#bbbbbb;stroke:#808080";

  // This is for display in Symbols dialog only
  if( style ) {
    repr->setAttribute( "style", style );
  }

  // BUG: Symbols don't work if defined outside of <defs>. Causes Inkscape
  // crash when trying to read in such a file.
  root->appendChild(repr);
  //defsrepr->appendChild(repr);
  Inkscape::GC::release(repr);

  // Uncomment this to get the previewDocument documents saved (useful for debugging)
  // FILE *fp = fopen (g_strconcat(symbol_id, ".svg", NULL), "w");
  // sp_repr_save_stream(previewDocument->getReprDoc(), fp);
  // fclose (fp);

  // Make sure previewDocument is up-to-date.
  previewDocument->getRoot()->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
  previewDocument->ensureUpToDate();

  // Make sure we have symbol in previewDocument
  SPObject *object_temp = previewDocument->getObjectById( "the_use" );
  previewDocument->getRoot()->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
  previewDocument->ensureUpToDate();

  // if( object_temp == NULL || !SP_IS_ITEM(object_temp) ) {
  //   //std::cout << "  previewDocument broken?" << std::endl;
  //   //return 0;
  // }

  SPItem *item = SP_ITEM(object_temp);

  Glib::ustring previewSizeString = previewSize->get_active_text();
  unsigned psize = atol( previewSizeString.c_str() );

  Glib::ustring previewScaleString = previewScale->get_active_text();
  int previewScaleRow = previewScale->get_active_row_number();

  /* Update to renderable state */
  Glib::ustring key = svg_preview_cache.cache_key(previewDocument->getURI(), symbol_id, psize);
  //std::cout << "  Key: " << key << std::endl;
  // FIX ME
  //Glib::RefPtr<Gdk::Pixbuf> pixbuf = Glib::wrap(svg_preview_cache.get_preview_from_cache(key));
  Glib::RefPtr<Gdk::Pixbuf> pixbuf = Glib::RefPtr<Gdk::Pixbuf>(0);

  // Find object's bbox in document.
  // Note symbols can have own viewport... ignore for now.
  //Geom::OptRect dbox = item->geometricBounds();
  Geom::OptRect dbox = item->documentVisualBounds();
  if (!dbox) {
    //std::cout << "  No dbox" << std::endl;
    return pixbuf;
  }

  if (!pixbuf) {

    /* Scale symbols to fit */
    double scale = 1.0;
    double width  = dbox->width();
    double height = dbox->height();
    if( width == 0.0 ) {
      width = 1.0;
    }
    if( height == 0.0 ) {
      height = 1.0;
    }

    switch (previewScaleRow) {
    case 0:
	/* Fit */
	scale = psize/std::max(width,height);
	break;
    case 1:
	/* Fit width */
	scale = psize/width;
	break;
    case 2:
	/* Fit height */
	scale = psize/height;
	break;
    default:
	scale = atof( previewScaleString.c_str() );
    }

    pixbuf = Glib::wrap(render_pixbuf(*drawing, scale, *dbox, psize));
    svg_preview_cache.set_preview_in_cache(key, pixbuf->gobj());
  }

  return pixbuf;
}

/*
 * Return empty doc to render symbols in.
 * Symbols are by default not rendered so a <use> element is
 * provided.
 */
SPDocument* SymbolsDialog::symbols_preview_doc()
{
  // BUG: <symbol> must be inside <defs>
  gchar const *buffer =
"<svg xmlns=\"http://www.w3.org/2000/svg\""
"     xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\""
"     xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\""
"     xmlns:xlink=\"http://www.w3.org/1999/xlink\">"
"  <defs id=\"defs\">"  
"    <symbol id=\"the_symbol\"/>"
"  </defs>"
"  <use id=\"the_use\" xlink:href=\"#the_symbol\"/>"
"</svg>";

  return SPDocument::createNewDocFromMem( buffer, strlen(buffer), FALSE );
}

void SymbolsDialog::setTargetDesktop(SPDesktop *desktop)
{
  if (this->currentDesktop != desktop) {
    this->currentDesktop = desktop;
    if( !symbolSets[symbolSet->get_active_text()] ) {
	// Symbol set is from Current document, update
	rebuild();
    }
  }
}

} //namespace Dialogs
} //namespace UI
} //namespace Inkscape
