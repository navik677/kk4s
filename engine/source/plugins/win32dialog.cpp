#include "ncbind/ncbind.hpp"

#include <string>

#define NCB_MODULE_NAME TJS_W("win32dialog.dll")

// kk4s: win32dialog.dll builds native Windows dialogs. KiriKiri Z games wrap it
// in script (`class WIN32DialogEX extends WIN32Dialog`, k2compat_*.tjs) and read
// its static members and Win32 constants while their classes are defined, so a
// stub must provide them or those scripts fail to load. No dialog can be shown
// here: open() reports "cancelled". The values are the real Win32 ones.
namespace {
struct Win32Const { const char* name; long long value; };

const Win32Const kConsts[] = {
    // window styles
    {"WS_POPUP", 0x80000000LL}, {"WS_VISIBLE", 0x10000000}, {"WS_CAPTION", 0x00C00000},
    {"WS_BORDER", 0x00800000}, {"WS_DLGFRAME", 0x00400000}, {"WS_SYSMENU", 0x00080000},
    {"WS_THICKFRAME", 0x00040000}, {"WS_MINIMIZEBOX", 0x00020000}, {"WS_MAXIMIZEBOX", 0x00010000},
    {"WS_GROUP", 0x00020000}, {"WS_TABSTOP", 0x00010000}, {"WS_VSCROLL", 0x00200000},
    {"WS_HSCROLL", 0x00100000}, {"WS_CLIPCHILDREN", 0x02000000}, {"WS_OVERLAPPEDWINDOW", 0x00CF0000},
    {"WS_EX_DLGMODALFRAME", 0x1}, {"WS_EX_TOPMOST", 0x8}, {"WS_EX_TOOLWINDOW", 0x80},
    {"WS_EX_WINDOWEDGE", 0x100}, {"WS_EX_CLIENTEDGE", 0x200}, {"WS_EX_APPWINDOW", 0x40000},
    {"GWL_STYLE", -16}, {"GWL_EXSTYLE", -20},
    // dialog styles
    {"DS_MODALFRAME", 0x80}, {"DS_SETFONT", 0x40}, {"DS_CENTER", 0x800}, {"DS_CENTERMOUSE", 0x1000},
    {"DS_FIXEDSYS", 0x8}, {"DS_SHELLFONT", 0x48},
    // button
    {"BS_PUSHBUTTON", 0}, {"BS_DEFPUSHBUTTON", 1}, {"BS_CHECKBOX", 2}, {"BS_AUTOCHECKBOX", 3},
    {"BS_RADIOBUTTON", 4}, {"BS_GROUPBOX", 7}, {"BS_AUTORADIOBUTTON", 9}, {"BS_ICON", 0x40},
    {"BS_FLAT", 0x8000}, {"BN_CLICKED", 0}, {"BM_GETCHECK", 0xF0}, {"BM_SETCHECK", 0xF1},
    {"BM_GETSTATE", 0xF2},
    // edit
    {"ES_LEFT", 0}, {"ES_MULTILINE", 0x4}, {"ES_AUTOVSCROLL", 0x40}, {"ES_AUTOHSCROLL", 0x80},
    {"ES_NOHIDESEL", 0x100}, {"ES_READONLY", 0x800}, {"ES_WANTRETURN", 0x1000}, {"EN_CHANGE", 0x300},
    {"EM_SETSEL", 0xB1}, {"EM_SCROLLCARET", 0xB7}, {"EM_SETREADONLY", 0xCF},
    // static
    {"SS_LEFT", 0}, {"SS_CENTER", 1}, {"SS_RIGHT", 2}, {"SS_ICON", 3}, {"SS_BITMAP", 0xE},
    {"SS_CENTERIMAGE", 0x200}, {"SS_SUNKEN", 0x1000},
    // list box
    {"LBS_NOTIFY", 1}, {"LBS_MULTIPLESEL", 8}, {"LBS_OWNERDRAWFIXED", 0x10},
    {"LB_ADDSTRING", 0x180}, {"LB_SETCURSEL", 0x186}, {"LB_GETSEL", 0x187}, {"LB_GETCURSEL", 0x188},
    {"LB_GETCOUNT", 0x18B}, {"LB_GETTOPINDEX", 0x18E}, {"LB_SETITEMHEIGHT", 0x1A0},
    {"LBN_SELCHANGE", 1}, {"LBN_DBLCLK", 2}, {"ODS_SELECTED", 1},
    // combo box (and ComboBoxEx)
    {"CBS_SIMPLE", 1}, {"CBS_DROPDOWN", 2}, {"CBS_DROPDOWNLIST", 3}, {"CB_ADDSTRING", 0x143},
    {"CB_DELETESTRING", 0x144}, {"CB_GETCOUNT", 0x146}, {"CB_GETCURSEL", 0x147}, {"CB_SETCURSEL", 0x14E},
    {"CBEM_INSERTITEMW", 0x40B}, {"CBEN_ENDEDITA", -805}, {"CBEN_ENDEDITW", -806},
    {"ICC_USEREX_CLASSES", 0x200},
    // list view / track bar / scroll bar
    {"LVS_REPORT", 1}, {"LVS_EX_GRIDLINES", 1}, {"LVM_DELETEALLITEMS", 0x1009},
    {"LVM_DELETECOLUMN", 0x101C}, {"LVM_INSERTCOLUMNW", 0x1061},
    {"TBS_HORZ", 0}, {"TBS_AUTOTICKS", 1}, {"TBM_GETPOS", 0x400}, {"TBM_SETPOS", 0x405},
    {"TBM_SETRANGEMIN", 0x407}, {"TBM_SETRANGEMAX", 0x408}, {"TBM_SETTICFREQ", 0x414},
    {"SBM_GETPOS", 0xE1},
    // GDI
    {"BLACK_BRUSH", 4}, {"WHITE_BRUSH", 0}, {"FW_NORMAL", 400}, {"FW_BOLD", 700},
    // control class atoms of dialog templates
    {"BUTTON", 0x80}, {"EDIT", 0x81}, {"STATIC", 0x82}, {"LISTBOX", 0x83}, {"SCROLLBAR", 0x84}, {"COMBOBOX", 0x85},
    // dialog results
    {"IDOK", 1}, {"IDCANCEL", 2}, {"IDABORT", 3},
};
} // namespace

static void InitPlugin_WIN32Dialog()
{
	ttstr script(
		TJS_W("class WIN32Dialog {")
		TJS_W("	function WIN32Dialog(owner) {}") // constructor; WIN32DialogEX calls it explicitly
		TJS_W("	function finalize() {}")
		TJS_W("	var modeless = 0;")
		TJS_W("	var hwnd = 0;")
		TJS_W("	function messageBox(message, caption, type) {return !System.inform(message, caption, 2);}")
		TJS_W("	function open() {return 2;}") // IDCANCEL
		TJS_W("	function close() {}")
		TJS_W("	function initCommonControlsEx() {return true;}")
		// kk4s: WIN32DialogEX.store() builds a DLGTEMPLATE from these two; the real
		// DLL returns a binary string. No dialog is ever shown here (open() above
		// cancels), so an empty template is enough - but they must be classes with
		// store(), not dictionaries (KAGZ's log window calls new WIN32Dialog.Header()).
		TJS_W("	function makeTemplate(header, items) {return \"\";}")
		TJS_W("	function getBaseUnits() {")
		TJS_W("		var a = [6, 13]; a.h = a.x = a.cx = a.w = a.width = 6; a.v = a.y = a.cy = a.height = 13; return a;")
		TJS_W("	}")
		TJS_W("}")
		TJS_W("class WIN32DialogHeader { function store(item) {} }")
		TJS_W("class WIN32DialogItems { function store(item) {} }")
		// Static members live on the class object itself (WIN32Dialog.Header).
		// KAGZ's modeless wrapper (k2compat_modeless.tjs) reads the last dialog
		// geometry as WIN32DialogEX.left/top/width/height, which resolves to the
		// base class' static members: the class inherits them (TJS class
		// property lookup follows the base class).
		TJS_W("WIN32Dialog.Header = WIN32DialogHeader;")
		TJS_W("WIN32Dialog.Items = WIN32DialogItems;")
		TJS_W("WIN32Dialog.left = 0; WIN32Dialog.top = 0;")
		TJS_W("WIN32Dialog.width = 0; WIN32Dialog.height = 0;")
		// k2compat_modeless.tjs calls these on the class (no window is created).
		// Note the ';': the lines above are concatenated into one script.
		TJS_W("WIN32Dialog.setPos = function(x, y) { WIN32Dialog.left = x; WIN32Dialog.top = y; };")
		TJS_W("WIN32Dialog.setSize = function(w, h) { WIN32Dialog.width = w; WIN32Dialog.height = h; };")
		TJS_W("WIN32Dialog.maximize = function() { };")
		TJS_W("WIN32Dialog.restoreMaximize = function() { };"));
	for(size_t i = 0; i < sizeof(kConsts) / sizeof(kConsts[0]); i++)
	{
		std::string line = std::string("WIN32Dialog.") + kConsts[i].name + " = " +
			std::to_string(kConsts[i].value) + ";";
		script += ttstr(line.c_str());
	}
	// common-control classes are addressed by name
	script += TJS_W("WIN32Dialog.TRACKBAR = \"msctls_trackbar32\"; WIN32Dialog.LISTVIEW = \"SysListView32\"; WIN32Dialog.COMBOBOXEX = \"ComboBoxEx32\";");
	TVPExecuteScript(script);
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WIN32Dialog);
