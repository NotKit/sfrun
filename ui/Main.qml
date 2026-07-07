import QtQuick 2.7
import Lomiri.Components 1.3
import io.thp.pyotherside 1.5

MainView {
    id: root
    objectName: "mainView"
    applicationName: "sfrun.thekit"
    automaticOrientation: true
    width: units.gu(45)
    height: units.gu(75)

    // ---- shared operation state (one streaming op at a time) ----
    property bool  opRunning: false
    property string opTitle: ""
    property string opLog: ""
    property int   opProgress: 0   // 0 = indeterminate / no download %
    property int   opRc: -1
    property var   opOnDone: null  // optional callback(rc) run when an op finishes

    property var statusRows: []
    property bool rootfsReady: false

    function refreshAll() {
        py.call("backend.status", [], function (rows) {
            root.statusRows = rows;
            var ready = false;
            for (var i = 0; i < rows.length; i++)
                if (rows[i].ok && rows[i].label.indexOf("guest rootfs") >= 0) ready = true;
            root.rootfsReady = ready;
        });
    }

    // Begin a streaming operation: reset the log, show the log page, then invoke
    // the backend call (passed as a closure so buttons stay declarative).
    function startOp(title, invoke, onDone) {
        if (root.opRunning) return;
        root.opTitle = title;
        root.opLog = "";
        root.opProgress = 0;
        root.opRc = -1;
        root.opOnDone = onDone || null;
        root.opRunning = true;
        stack.push(logPage);
        invoke();
    }

    Python {
        id: py
        Component.onCompleted: {
            addImportPath(Qt.resolvedUrl("."));
            importModule("backend", function () { root.refreshAll(); });
        }
        onError: console.log("pyotherside error: " + traceback)
        onReceived: {
            var ev = data[0], val = data[2];
            if (ev === "line")          root.opLog += val + "\n";
            else if (ev === "progress") root.opProgress = val;
            else if (ev === "done") {
                root.opRc = val;
                root.opRunning = false;
                root.opLog += (val === 0 ? "\n✓ finished\n"
                                         : "\n✗ failed (exit " + val + ")\n");
                root.refreshAll();
                if (root.opOnDone) {
                    var cb = root.opOnDone;
                    root.opOnDone = null;
                    cb(val);
                }
            }
        }
    }

    PageStack {
        id: stack
        Component.onCompleted: push(homePage)
    }

    // ======================================================= HOME
    Component {
        id: homePage
        Page {
            header: PageHeader {
                id: hHome
                title: i18n.tr("Sailfish Apps")
                subtitle: root.rootfsReady ? i18n.tr("Runtime ready")
                                           : i18n.tr("Runtime not set up yet")
            }
            Flickable {
                anchors.fill: parent
                anchors.topMargin: hHome.height
                contentHeight: col.height + units.gu(4)
                Column {
                    id: col
                    width: parent.width
                    spacing: units.gu(2)
                    anchors.margins: units.gu(2)
                    anchors.left: parent.left
                    anchors.right: parent.right

                    Item { width: 1; height: units.gu(1) }

                    // --- status card ---
                    Rectangle {
                        width: parent.width
                        height: statusCol.height + units.gu(4)
                        radius: units.gu(1)
                        color: theme.palette.normal.foreground
                        Column {
                            id: statusCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: units.gu(2) }
                            spacing: units.gu(1)
                            Label { text: i18n.tr("Status"); textSize: Label.Large }
                            Repeater {
                                model: root.statusRows
                                delegate: Row {
                                    spacing: units.gu(1)
                                    width: statusCol.width
                                    Label {
                                        text: modelData.ok ? "✓" : "✗"
                                        color: modelData.ok ? LomiriColors.green : LomiriColors.red
                                    }
                                    Label {
                                        text: modelData.label
                                        width: parent.width - units.gu(3)
                                        elide: Text.ElideRight
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                            Label {
                                visible: root.statusRows.length === 0
                                text: i18n.tr("Checking…")
                                color: theme.palette.normal.backgroundSecondaryText
                            }
                        }
                    }

                    Button {
                        width: parent.width
                        text: root.rootfsReady ? i18n.tr("Re-download rootfs")
                                               : i18n.tr("Bootstrap rootfs (download)")
                        color: root.rootfsReady ? theme.palette.normal.base
                                                : theme.palette.normal.positive
                        onClicked: root.startOp(i18n.tr("Bootstrap"),
                            function () { py.call("backend.bootstrap", [""], function () {}); })
                    }
                    Button {
                        width: parent.width
                        text: i18n.tr("Run setup (GL + packages + theme)")
                        onClicked: root.startOp(i18n.tr("Setup"),
                            function () { py.call("backend.setup", [], function () {}); })
                    }
                    Rectangle {
                        width: parent.width
                        height: units.dp(1)
                        color: theme.palette.normal.base
                    }
                    Button {
                        width: parent.width
                        text: i18n.tr("Chum store")
                        color: theme.palette.normal.positive
                        enabled: root.rootfsReady
                        onClicked: stack.push(chumPage)
                    }
                    Button {
                        width: parent.width
                        text: i18n.tr("Install app from .rpm…")
                        onClicked: stack.push(installPage)
                    }
                    Button {
                        width: parent.width
                        text: i18n.tr("My apps")
                        onClicked: stack.push(appsPage)
                    }
                }
            }
        }
    }

    // ======================================================= LOG
    Component {
        id: logPage
        Page {
            header: PageHeader { id: hLog; title: root.opTitle }
            Column {
                anchors { fill: parent; topMargin: hLog.height + units.gu(1); margins: units.gu(2) }
                spacing: units.gu(1)
                Row {
                    width: parent.width
                    spacing: units.gu(1)
                    height: units.gu(4)
                    ActivityIndicator { running: root.opRunning; visible: root.opRunning && root.opProgress === 0 }
                    ProgressBar {
                        visible: root.opProgress > 0
                        width: parent.width - units.gu(6)
                        minimumValue: 0; maximumValue: 100; value: root.opProgress
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.opProgress > 0 ? root.opProgress + "%"
                              : (root.opRunning ? i18n.tr("working…") : "")
                    }
                }
                TextArea {
                    id: logArea
                    width: parent.width
                    height: parent.height - units.gu(11)
                    readOnly: true
                    font.family: "Ubuntu Mono"
                    text: root.opLog
                    onTextChanged: cursorPosition = text.length
                }
                Button {
                    width: parent.width
                    text: i18n.tr("Close")
                    enabled: !root.opRunning
                    color: theme.palette.normal.positive
                    onClicked: stack.pop()
                }
            }
        }
    }

    // ======================================================= INSTALL
    Component {
        id: installPage
        Page {
            id: ip
            property var rpms: []
            function refresh() { py.call("backend.list_rpms", [], function (r) { ip.rpms = r; }); }
            Component.onCompleted: refresh()
            header: PageHeader {
                id: hInstall
                title: i18n.tr("Install from .rpm")
                trailingActionBar.actions: Action {
                    iconName: "reload"; text: i18n.tr("Refresh"); onTriggered: ip.refresh()
                }
            }
            Column {
                anchors { fill: parent; topMargin: hInstall.height }
                spacing: units.gu(1)
                Item { width: 1; height: units.gu(1) }
                Label {
                    width: parent.width - units.gu(4)
                    x: units.gu(2)
                    wrapMode: Text.WordWrap
                    text: i18n.tr("Put .rpm files in Downloads, then pick one below.")
                    color: theme.palette.normal.backgroundSecondaryText
                }
                ListView {
                    width: parent.width
                    height: ip.height - hInstall.height - units.gu(8)
                    clip: true
                    model: ip.rpms
                    delegate: ListItem {
                        height: layout.height + units.gu(2)
                        ListItemLayout {
                            id: layout
                            title.text: modelData.name
                            title.elide: Text.ElideMiddle
                            Button {
                                SlotsLayout.position: SlotsLayout.Trailing
                                text: i18n.tr("Install")
                                color: theme.palette.normal.positive
                                onClicked: {
                                    stack.pop();
                                    root.startOp(i18n.tr("Install ") + modelData.name,
                                        function () { py.call("backend.install_rpm", [modelData.path], function () {}); });
                                }
                            }
                        }
                    }
                }
                Label {
                    visible: ip.rpms.length === 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: i18n.tr("No .rpm files found in Downloads")
                    color: theme.palette.normal.backgroundSecondaryText
                }
            }
        }
    }

    // ======================================================= APPS
    Component {
        id: appsPage
        Page {
            id: ap
            property var apps: []
            function refresh() { py.call("backend.list_apps", [], function (a) { ap.apps = a; }); }
            Component.onCompleted: refresh()
            header: PageHeader {
                id: hApps
                title: i18n.tr("My apps")
                trailingActionBar.actions: Action {
                    iconName: "reload"; text: i18n.tr("Refresh"); onTriggered: ap.refresh()
                }
            }
            Column {
                anchors { fill: parent; topMargin: hApps.height }
                ListView {
                    width: parent.width
                    height: ap.height - hApps.height
                    clip: true
                    model: ap.apps
                    delegate: ListItem {
                        height: al.height + units.gu(2)
                        ListItemLayout {
                            id: al
                            title.text: modelData.name
                            subtitle.text: modelData.id
                            Button {
                                SlotsLayout.position: SlotsLayout.Trailing
                                text: i18n.tr("Run")
                                color: theme.palette.normal.positive
                                onClicked: py.call("backend.run_app", [modelData.id], function () {})
                            }
                        }
                        leadingActions: ListItemActions {
                            actions: Action {
                                iconName: modelData.launcher ? "starred" : "add"
                                text: i18n.tr("Add to app drawer")
                                onTriggered: root.startOp(i18n.tr("Add launcher"),
                                    function () { py.call("backend.make_desktop", [modelData.id], function () {}); })
                            }
                        }
                    }
                }
                Label {
                    visible: ap.apps.length === 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: i18n.tr("No apps installed yet")
                    color: theme.palette.normal.backgroundSecondaryText
                }
            }
        }
    }

    // ======================================================= CHUM STORE
    Component {
        id: chumPage
        Page {
            id: cp
            property var allApps: []     // full catalog
            property var apps: []        // filtered view shown in the list
            property string filter: ""
            property bool loading: false

            function applyFilter() {
                if (cp.filter.length === 0) { cp.apps = cp.allApps; return; }
                var f = cp.filter.toLowerCase(), r = [];
                for (var i = 0; i < cp.allApps.length; i++) {
                    var a = cp.allApps[i];
                    if (a.name.toLowerCase().indexOf(f) >= 0 ||
                        a.summary.toLowerCase().indexOf(f) >= 0) r.push(a);
                }
                cp.apps = r;
            }
            function reload() {
                cp.loading = true;
                py.call("backend.chum_list", [], function (l) {
                    cp.allApps = l; cp.loading = false; cp.applyFilter();
                });
            }
            Component.onCompleted: reload()

            header: PageHeader {
                id: hChum
                title: i18n.tr("Chum store")
                subtitle: cp.allApps.length > 0 ? cp.apps.length + " / " + cp.allApps.length : ""
                trailingActionBar.actions: Action {
                    iconName: "reload"
                    text: i18n.tr("Refresh catalog")
                    onTriggered: root.startOp(i18n.tr("Enable / refresh Chum"),
                        function () { py.call("backend.chum_enable", [], function () {}); },
                        function (rc) { if (rc === 0) cp.reload(); })
                }
            }
            Column {
                anchors { fill: parent; topMargin: hChum.height }
                spacing: units.gu(1)
                TextField {
                    id: searchField
                    width: parent.width - units.gu(2)
                    x: units.gu(1)
                    placeholderText: i18n.tr("Search %1 packages").arg(cp.allApps.length)
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onDisplayTextChanged: { cp.filter = displayText; cp.applyFilter(); }
                }
                ListView {
                    width: parent.width
                    height: cp.height - hChum.height - searchField.height - units.gu(3)
                    clip: true
                    model: cp.apps
                    delegate: ListItem {
                        height: cl.height + units.gu(2)
                        ListItemLayout {
                            id: cl
                            title.text: modelData.name
                            subtitle.text: modelData.summary
                            subtitle.maximumLineCount: 2
                            subtitle.wrapMode: Text.WordWrap
                            Button {
                                SlotsLayout.position: SlotsLayout.Trailing
                                text: modelData.installed ? i18n.tr("Installed") : i18n.tr("Install")
                                enabled: !modelData.installed
                                color: modelData.installed ? theme.palette.normal.base
                                                           : theme.palette.normal.positive
                                onClicked: root.startOp(i18n.tr("Install ") + modelData.name,
                                    function () { py.call("backend.install_pkg", [modelData.name], function () {}); },
                                    function (rc) { if (rc === 0) cp.reload(); })
                            }
                        }
                    }
                }
                Label {
                    visible: !cp.loading && cp.allApps.length === 0
                    width: parent.width - units.gu(4)
                    x: units.gu(2)
                    wrapMode: Text.WordWrap
                    text: i18n.tr("Tap the refresh icon to enable the Chum catalog.")
                    color: theme.palette.normal.backgroundSecondaryText
                }
            }
        }
    }
}
