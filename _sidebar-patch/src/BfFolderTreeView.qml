import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls 1.4
import QtQuick.Controls.Styles 1.4
import QtQml.Models 2.15
import QtQuick.Layouts 1.15
import QtGraphicalEffects 1.15
import ".."
import SkinSetting 1.0


Item {
    id: tree_view
    property color ue_click_color: skin_model.getColor(BFSkinSetting.KUEClickColor)+skin_model.color_change_sign
    property color ue_hover_color: skin_model.getColor(BFSkinSetting.KUEHoverColor)+skin_model.color_change_sign
    property color background_color: skin_model.getColor(BFSkinSetting.KCommonBackgroundColor)+skin_model.color_change_sign
    property color font_color: skin_model.getColor(BFSkinSetting.KCommonSecondaryColor)+skin_model.color_change_sign
    property color font_select_color: skin_model.getColor(BFSkinSetting.kFontColor)+skin_model.color_change_sign
    property color common_major_color: skin_model.getColor(BFSkinSetting.KCommonMajorColor)+skin_model.color_change_sign
    property color brand_color: skin_model.getColor(BFSkinSetting.KBrandColor)+skin_model.color_change_sign
    property color font_number_color: skin_model.getColor(BFSkinSetting.KCommonAuxiliaryColor)+skin_model.color_change_sign
    property color second_color: skin_model.getColor(BFSkinSetting.KCommonSecondaryColor)+skin_model.color_change_sign
    property color not_clicked_color: skin_model.getColor(BFSkinSetting.KCommonNotClickedColor)+skin_model.color_change_sign
    property alias  tree_sel: sel
    
    property color not_clicked_color_60: skin_model.getColor(BFSkinSetting.KCommonNotClickedColor_60)+skin_model.color_change_sign
    
    property color line_color: skin_model.getColor(BFSkinSetting.KLineColor)+skin_model.color_change_sign
    
    property color not_clicked_color_80: skin_model.getColor(BFSkinSetting.KCommonNotClickedColor_80)+skin_model.color_change_sign
    property color handleNormalColor: not_clicked_color_60  //滑块颜色
    property color handleHoverColor: not_clicked_color
    property color handlePressColor: not_clicked_color
  
    
    
    
    signal signSelection();
    signal signNoSelection();
    property int drop_area_height : 5
    property var dragItemIndex:undefined
    property var currentIndex:undefined
    property int selectionRevision: 0
    
    property bool is_branch_hover: false
    property bool isExpand: true
    //左侧括号高亮
    property var branch_hover_current_item:undefined
    //首行高亮
    property bool root_branch_hover:false

    property bool tree_view_visible: view.visible
    //展开节点
    property var expand_item:undefined
    property bool  analyse_hover: false
    
    property var right_index:undefined
    
    property var hover_index:undefined
    
    signal signRightPress();
    
    property real view_position: 0
    property bool model_update: false
    
    
    
    QtObject
    {
        id: objPrivate
        property    int         mBranchWidth:           16
    }

    property	bool	mShowLines:             true
    property	bool	mShowPlusMinus:         true                                //  <=> CONTROL LIST:   ShowPlusMinus
    property	bool	mShowRootLines:         false

    Timer {
        id: timer_update_view
        interval: 50
        repeat: false //是否重复定时,默认为false
        running: false //是否开启定时，默认是false，当为true的时候，进入此界面就开始定时
        triggeredOnStart: false // 是否开启定时就触发onTriggered，一些特殊用户可以用来设置初始值。
        onTriggered: {
            view.visible =!view.visible
            view.visible =!view.visible
        }
    }
    property int scroll_row: -1
    
    Timer {
        id: auto_scroll
        interval: 500
        repeat: false //是否重复定时,默认为false
        running: false //是否开启定时，默认是false，当为true的时候，进入此界面就开始定时
        triggeredOnStart: false // 是否开启定时就触发onTriggered，一些特殊用户可以用来设置初始值。
        onTriggered: {
            
            if(scroll_row ===0)
            {
                verticalBar.decrease()
            }
            else if(scroll_row===1)
            {
                verticalBar.increase()
            }
            
        }
    }
    
    Timer {
        id: expend_item_timer
        interval: 1500
        repeat: false //是否重复定时,默认为false
        running: false //是否开启定时，默认是false，当为true的时候，进入此界面就开始定时
        triggeredOnStart: false // 是否开启定时就触发onTriggered，一些特殊用户可以用来设置初始值。
        onTriggered: {
            //console.log("----------------expand")
            if(expand_item!==undefined && hover_index ===expand_item)
            {
                 view.expand(expand_item);
            }
        }
    }
    
    Timer {
           id: time_update_view_contony
           interval: 1
           repeat: false //是否重复定时,默认为false
           running: false //是否开启定时，默认是false，当为true的时候，进入此界面就开始定时
           triggeredOnStart: false // 是否开启定时就触发onTriggered，一些特殊用户可以用来设置初始值。
           onTriggered: {
              view.flickableItem.contentY =  view_position
              //view.visible =!view.visible 
           }
       }
    
    Connections
    {
        target: model_data
        function onModelAboutToBeReset()
        {
            control.ClearExpandState();
            //console.log("    ++++++++++++++      "+menu_control.is_sort)
            if(!menu_control.is_sort)
            {
                var ret = model_data.getPersistentIndexList();
                //console.log(ret.length)
                for(var i = 0; i < ret.length;i++){
                    if(view.isExpanded(ret[i])){
                        //console.log("expand" + ret[i])
                        control.SaveExpandState(ret[i]);
                    }
                }
            }
            view_position = view.flickableItem.contentY
        }
        
        function onModelReset()
        {
            model_update=true
            timer_update_view.running =true
        }
        
        
        function onSignExpandSearch()
        {
            var ret = model_data.getPersistentIndexList();
            //console.log("expand-------------------" +ret.length)
            for(var i = 0; i < ret.length;i++){
                view.expand(ret[i])
            }
        }
    }
    
    Connections
    {
        target: menu_control
        function onSignPostionView(expand_index)
        {
            var row = 0;
            var new_index = expand_index;
            while(new_index.valid){
                row += new_index.row;
                new_index = new_index.parent
            }
            
            view.positionViewAtRow(row,TreeView.Beginning)
        }
        
        function onSigExpandCollapseSibling(indexs)
        {
           var expand_state = judegeExpandstate(indexs)

            for(var i= 0;  i<indexs.length;i++){
                 var r1  =indexs[i];
                 expandCollapseIndex(r1,expand_state)
             }
        }
        
        function onSigExpandCollapseAll(indexs)
        {
            var expand_state = judegeExpandstate(indexs)
            menu_control.expandAllIndexs(expand_state);
        }
        
        function onSigExpandCollapseIndex(index,state)
        {
            expandCollapseIndex(index,state)
        }
    }
    
    
    Connections
    {
        target: control
        function onExpandTreeItem(expand_index)
        {
            view.expand(expand_index);
        }

        function onExpandTreeItems(indexs)
        {
            console.log("onExpandTreeItems-------------------" +indexs.length)
            for(var i= 0;  i<indexs.length;i++){
                 var r1  =indexs[i];
                 view.expand(r1);
             }
        }
        
        function onSignExpand(index){
            if(view.isExpanded(index)){
                control.indexRow(index)
            }
            
        }
        
        
        function onSignClearSelects()
        {
            clearSelect()
        }
        
        
        function onSignPostionView(expand_index)
        {
            var row = 0;
            var new_index = expand_index;
            while(new_index.valid){
                row += new_index.row;
                new_index = new_index.parent
            }
            
            view.positionViewAtRow(row,TreeView.Beginning)
        }
        
        
        function onSignExpandTreeState(expand_index,state)
        {
            if(state)
            {
                view.expand(expand_index);
            }
            else
            {
                view.collapse(expand_index);
            }
        }
        
        function onSignalAddedFolder(expand_index)
        {
            expandParent(expand_index)
            
            var row = 0;
            var new_index = expand_index;
            while(new_index.valid){
                row += new_index.row;
                new_index = new_index.parent
            }
            
           
            
            view.positionViewAtRow(row,TreeView.Beginning)
            view.renameItem(expand_index)
            
            sel.setCurrentIndex(expand_index, ItemSelectionModel.ClearAndSelect)
            
        }
        
        function onSignSelectFolder(select_index,row)
        {
         
            //sel.clearSelection()
            clearSelect()
            
            var tem_parent = select_index.parent
            while(tem_parent.valid){
                view.expand(tem_parent)
                tem_parent =tem_parent.parent
            }
            
//            var row = 0;
//            var new_index = select_index;
//            while(new_index.valid){
//                console.log("new_index---------" + new_index.row)
//                row += new_index.row;
//                new_index = new_index.parent
                
//            }
            
            
            
//            //最顶层的 index row
//            var top_row = last_index.row();
            
            
//            var mode =last_index.model();
//            for (var i=0;i<top_row;++i)
//            {
//                var child_index = mode.index(i, 0);
                
//                if (child_index.isValid())
//                {
                    
                    
//                }
//            }
            
            
            
            
            // Resolve the flattened row after expansion; reveal only if off screen.
            for (var visibleRow = 0; visibleRow < view.flickableItem.count; ++visibleRow) {
                if (view.__model.mapRowToModelIndex(visibleRow) === select_index) {
                    view.positionViewAtRow(visibleRow, ListView.Contain)
                    break
                }
            }
            sel.setCurrentIndex(select_index, ItemSelectionModel.ClearAndSelect)
            signSelection()
            // sel.setCurrentIndex(select_index,Qt.ImCurrentSelection)
        }
        
        
        function onSignSelectRoot()
        {
            clearCurrentFolder()
            //console.log("onSignSelectRoot")
            clearSelect()
            
            label_bg.current_state =2
            signSelection()
        }
        
        
        function onSignAfterLoadLibrary()
        {
            view.visible = true
            isExpand=view.visible
            search_edit_rect.visible =false
            textEdit.clear()
            
        }
        
    }
    
    
    function judegeExpandstate(indexs)
    {

        var no_children = 0
        var expand_index=[]
        for(var i = 0; i < indexs.length;i++){
            var ret  =indexs[i];
            if(model_data.hasChildren(ret)){
                if(view.isExpanded(ret)){
                    expand_index.push(ret);
                }
            }
            else{
                no_children++
            }
        }
        
        var has_hasChildren =indexs.length-no_children

         var expand_state=false;
        if(has_hasChildren/2 ===expand_index.length)
        {
             if(model_data.hasChildren(ret))
             {
                 if(right_index===undefined){
                     if(sel.currentIndex!==undefined){
                          expand_state = view.isExpanded(sel.currentIndex)
                     }
                 }
                 else{
                     expand_state = view.isExpanded(right_index)
                 }
                 
             }
             else{
                 expand_state = true
             }
        }
        else if( expand_index.length>(has_hasChildren/2)){
             expand_state = true
        }
        else{
             expand_state = false
        }

        //console.log(indexs.length+"   "+no_children+"  " +has_hasChildren +"   "+ expand_index.length  +"     " +expand_state )
        return expand_state
    }
    
    
    function expandCollapseIndex(index,expand_state)
    {
        if(model_data.hasChildren(index))
        {
            var row = 0;
            var new_index = index;
            while(new_index.valid){
                row += new_index.row;
                new_index = new_index.parent
            }
            
            view.positionViewAtRow(row,TreeView.Beginning)
            
            if(expand_state)
                 view.collapse(index)
            else
                 view.expand(index);
        }
    }

    function expandParent(expand_index){
        var tem_parent = expand_index
        while(tem_parent.valid){
            view.expand(tem_parent)
            tem_parent =tem_parent.parent
        }
    }
    
    
    function isFolderSelected(index)
    {
        // Bind to actual model indexes, including selection-only changes.
        var revision = selectionRevision
        return index.valid && (sel.isSelected(index) || (!sel.hasSelection && sel.currentIndex === index))
    }

    function clearCurrentFolder()
    {
        sel.clearCurrentIndex()
    }

    function clearSelect()
    {
        signNoSelection()
        label_bg.current_state=0
        //label.color =font_color
        sel.clearSelection()
        right_index = undefined
    }
    
    
    function searchFolder()
    {
        if(view.activeFocus)
        {
            search_btn.clicked()
        }
    }
    
    function addFolder()
    {
        if(view.activeFocus)
        {
            add_btn.clicked()
        }
    }
    
    
    function addSubFolder()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.SlotCreateSubFolder()
        }
    }
    
    
    function scanRepate()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.SlotSacnFilderRepeatFile()
        }
    }
    
    function openFolder()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.SlotCheckLoadFolder()
        }
    }
    
    function moveToCloud()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.SlotTransferFolderToCloud()
        }
    }
    
    
    function reAnalysis()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.SlotReAnalyzeSource()
        }
    }
    
    
    function delFolder()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.SlotLeaveRecycleBin()
        }
    }
    
    
    function renameFolder()
    {
        if(view.activeFocus)
        {
            view.renameItem(sel.currentIndex)
        }
    }
    
    
    function expandCollapseSibling ()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.slotExpandCollapseSibling()
        }
    }
    
    function expandCollapseAll()
    {
        if(view.activeFocus)
        {
            menu_control.setCurrentIndexs(sel.selectedIndexes)
            menu_control.slotExpandCollapseAll()
        }
    }
    
    ItemSelectionModel {
        id: sel
        model: model_data
        
        
        onSelectionChanged:
        {
            selectionRevision++
            label_bg.current_state=0
            //label.color =font_color
            var selected_length = selected.length;
            if( selected_length === 0){
                // 没有选中不需要处理
                signNoSelection()
                return;
            }
            
            signSelection();
            var current_selected_length = sel.selectedIndexes.length;
            
//            if(selected_length === 1 && current_selected_length <= 1){
//                // 单选切换
//                control.selectionChanged(selected,deselected)
//                return;
//            }
            
            
            var indexes = sel.selectedIndexes;
            var tmpIndex = view.currentIndex.parent;
            var vaild = [];
            var invalid = [];
            if(tmpIndex.valid){
                invalid = Array.from(indexes).filter(r => r.parent !== tmpIndex)
                vaild = Array.from(indexes).filter(r => r.parent === tmpIndex)
            }else{
                invalid = Array.from(indexes).filter(r => r.parent.valid)
                vaild = Array.from(indexes).filter(r => !r.parent.valid)
            }
            
            if(invalid.length !== 0){
                // 不合法取消选择
                for (var i = 0; i < invalid.length;i++){
                    sel.select(invalid[i], ItemSelectionModel.Deselect);
                }
            }
            
            if(sel.selectedIndexes.length === 1){
                // 只剩一个 要不要切换？
            }
            
            control.selectMoreIndex(sel.selectedIndexes)
        }
    }
    
    Rectangle {
        id: folder_item
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 6
        anchors.right: parent.right
        anchors.rightMargin: 15
        height: 30
        
        radius:6
        color:background_color
        
        Rectangle{
            
            property int current_state: 0
            id:label_bg
            color:{
                if(current_state===0){
                     return background_color
                }
                
                if(current_state===1){
                    
                    return ue_hover_color
                }

                if(current_state===2){
                    return ue_click_color
                }
                
                return background_color
            }
            
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.top: parent.top
            height: 30
            width: 67
            radius: 6
            
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 5
                text:"文件夹"
                color:
                {
                    if(label_bg.current_state===0){
                         return font_color
                    }
                    
                    if(label_bg.current_state===1){
                        
                        return font_color
                    }
    
                    return common_major_color
                }
                font.pixelSize: 13
                font.bold:true
            }
            
            
            Image{//节点展开标记图
                id:image
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 8
                anchors.topMargin:12
                source:
                {
                    if(!isExpand)
                    {
                        if(root_branch_hover)
                        {
                            return "file:///"+skin_model.GetResourceAbsolutePath("icon_close_hover.svg")+skin_model.color_change_sign;
                        }
                        
                        return "file:///"+skin_model.GetResourceAbsolutePath("icon_close_normal.svg")+skin_model.color_change_sign;
                    }
                    else
                    {
                        if(root_branch_hover)
                        {
                            return "file:///"+skin_model.GetResourceAbsolutePath("icon_open_hover.svg")+skin_model.color_change_sign;
                        }
                        
                        return "file:///"+skin_model.GetResourceAbsolutePath("icon_open_normal.svg")+skin_model.color_change_sign;
                    }
                }
                
                width:8
                height: 8
                
                //smooth:true
                //mipmap:true
                MouseArea
                {
                    id:image_mouse_area
                    anchors.fill: parent
                    hoverEnabled: true
                    z:10
                    onEntered: {
                        root_branch_hover = true
                        label_bg.current_state=1
                    }
                    
                    onExited:
                    {
                        root_branch_hover = false
                        if(!isExpand)
                        {
                            label_bg.current_state=0
                        }
                    }
                    
                    onClicked:
                    {
                        //console.log("click   image")
                        label_bg.current_state=2
                        control.selectionRoot()
                        view.visible = !view.visible
                        isExpand = view.visible
                    }
                }
            }
            
            MouseArea
            {
                id:folder_mouse_area
                anchors.fill: parent
                hoverEnabled: true
                z:-10
                onEntered: {
                    //root_branch_hover = true
                    if(label_bg.current_state ===0)
                    {
                       label_bg.current_state =1
                    }
                }
                
                onExited:
                {
                    if(label_bg.current_state ===1)
                    {
                        label_bg.current_state =0
                    }
                }
                
                onClicked:
                {
                    if(sel.hasSelection)
                    {
                        sel.clearSelection()
                    }
                    signSelection();
                    label_bg.current_state =2
                    control.selectionRoot()
                }
            }
        }
        
        
        
        BfButton{
            id :add_btn
            btnImgNormal: "file:///"+skin_model.GetResourceAbsolutePath("icon_add_normal.svg")+skin_model.color_change_sign;
            btnImgHovered:"file:///"+skin_model.GetResourceAbsolutePath("icon_add_hover.svg")+skin_model.color_change_sign;
            btnImgPressed:"file:///"+skin_model.GetResourceAbsolutePath("icon_add_normal.svg")+skin_model.color_change_sign;
            btnImgDisbaled: "file:///"+skin_model.GetResourceAbsolutePath("icon_add_normal.svg")+skin_model.color_change_sign;
            tipText:
            {
                var short_cut_str="";
                if(Qt.platform.os === "windows" || Qt.platform.os==="winrt"){
                    short_cut_str = " (Ctrl+N)"
                }
                else{
                    short_cut_str = " (" + skin_model.keyToString("Ctrl+N") + ")"
                }
                return qsTr("新增同级文件夹 ")+short_cut_str+skin_model.language
            }

            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 1
            
            onClicked:
            {
                //console.log(" control.AddFloder(view.currentIndex)")
                view.visible =true
                isExpand=true
                control.AddFolder(sel.currentIndex)
            }
            z:10
        }
        
        BfButton{
            id :search_btn
            btnImgNormal: "file:///"+skin_model.GetResourceAbsolutePath("icon_search_normal.svg")+skin_model.color_change_sign
            btnImgHovered:"file:///"+skin_model.GetResourceAbsolutePath("icon_search_hover.svg")+skin_model.color_change_sign
            btnImgPressed:"file:///"+skin_model.GetResourceAbsolutePath("icon_search_normal.svg")+skin_model.color_change_sign
            btnImgDisbaled: "file:///"+skin_model.GetResourceAbsolutePath("icon_search_normal.svg")+skin_model.color_change_sign
            tipText: 
            {
                var short_cut_str="";
                if(Qt.platform.os === "windows" || Qt.platform.os==="winrt"){
                    short_cut_str = " (Ctrl+Shift+F)"
                }
                else{
                    short_cut_str = " (" + skin_model.keyToString("Ctrl+Shift+F") + ")"
                }
                
                return qsTr("搜索文件夹")+short_cut_str+skin_model.language
            }
            
            
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: add_btn.left
            anchors.rightMargin: 1
            
            onClicked:
            {
                search_edit_rect.visible =true
                search_edit_rect.forceActiveFocus()
                textEdit.forceActiveFocus()
                sel.clearSelection()

                //搜索记录 树的状态
                tree_view_visible = view.visible
                control.clearSearchState();
                var ret = model_data.getPersistentIndexList();
                for(var i = 0; i < ret.length;i++){
                   var expand_state =view.isExpanded(ret[i])
                   control.saveSearchBefroreExpandState(ret[i],expand_state);
                }
                
                verticalBar.position =0
            }
            z:10
        }
        
        
        BfButton{
            id :recycle_btn
            btnImgNormal: "file:///"+skin_model.GetResourceAbsolutePath("icon_title_recycle_normal.svg")+skin_model.color_change_sign
            btnImgHovered:"file:///"+skin_model.GetResourceAbsolutePath("icon_title_recycle_hover.svg")+skin_model.color_change_sign
            btnImgPressed:"file:///"+skin_model.GetResourceAbsolutePath("icon_title_recycle_normal.svg")+skin_model.color_change_sign
            btnImgDisbaled: "file:///"+skin_model.GetResourceAbsolutePath("icon_title_recycle_normal.svg")+skin_model.color_change_sign
            tipText: 
            {
                return  qsTr("回收站 ")+ "(" + control.open_recycle_tips_str + ")"+skin_model.language
            }
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: search_btn.left
            anchors.rightMargin: 1
            
            onClicked:
            {
                control.openRecycle()
            }
            z:10

        }
        
        AnimatedImage {
                id: animated
                z:10
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: recycle_btn.left
                anchors.rightMargin: 9
                source: "file:///"+skin_model.GetResourceAbsolutePath("analyse.gif")
         
                paused: !control.is_anaylse
                visible: control.is_anaylse
                width: 14
                height: 14
                
                MouseArea
                {
                    anchors.fill: parent
                    hoverEnabled: true
                    
                    
                    onEntered: 
                    {
                        analyse_hover =true
                    }
                    
                    onExited: 
                    {
                        analyse_hover =false
                    }
                }
                
                
                ToolTip {
                    id: toolTip
                    contentItem: Text {
                        text: qsTr("分析中")+control.anaylse_str
                        font.pixelSize: 13
                        color:font_color
                    }
                    background: Rectangle {
                        color:background_color
                        radius:6
                        border.color:ue_hover_color
                    }
            
                    visible: analyse_hover 
                    delay: 0
                }
                
            }

        Rectangle {
            id: search_edit_rect
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 15
            
            visible: false
            color: common_major_color
            border.color: background_color
            radius: 4
            z:10
            
            BfSearch{
                id:textEdit
                anchors.fill: parent
                
                placeholderText :qsTr("查找文件夹")
                onSignCloseSearch:
                {
                    view.visible = tree_view_visible
                    control.restoreSearchExpandState();
                    
                    search_edit_rect.visible =false
                }
                
                onTextChanged:
                {
                    if(view.visible===false)
                    {
                        view.visible = true
                    }
                    verticalBar.position =0
                    control.UpdateSearchTextChanged(text)
                    
                   
                    view.repaintLine()
                }
            }
        }
        
    }
    
    BFTreeView{
        id: view
        selectionMode: SelectionMode.ExtendedSelection
         
        signal renameItem(var index)
        signal disrenameItem(var index)
        signal repaintLine()
        
        model: model_data
        selection: sel
        anchors.left: parent.left
        anchors.top: folder_item.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 15
        headerVisible: false
        frameVisible: false    //隐藏边框
        backgroundVisible: false
        
        TableViewColumn { //添加一列
            role: "displayname"  //这个role的名字要跟你在model中定义的列的名字一模一样，
            resizable: true
        }
        
        style:treeViewStyle
        itemDelegate:Loader
        {
            id:itemLoader
            property var index_data: styleData
            property variant stringList:
            {
                if(styleData.value==="")
                {
                    var temp_str="/";
                    return temp_str.split('/')
                }
                
                return styleData.value.split('/')
            }
            property string number_count: stringList[1]
            property string file_name: stringList[0]
            
            sourceComponent:
            {
                //console.log("test_number  "+ file_number +"  ")
                return item_delegate
            }
            
            onLoaded: {
                
            }
            
            onFocusChanged: {
                //console.log("treeview onFocusChanged.")
            }
            
        }
        
        DropArea {
            id: drop_content
            anchors.fill: parent
            keys: [ "text/uri-list","FileFromView","chromium/x-web-custom-data"]
            onDropped: {
                tree_view.dragItemIndex = undefined
                hover_index = undefined
                var dropformats = drop.formats;
                if(dropformats.indexOf("chromium/x-web-custom-data") !== -1)
                {
                    control.dropWebDataFolderRoot();
                    drop.accept();
                    return
                }

                if(drop.source && drop.source.objectName=="BFBaseView")
                {
                    //外部资源往树拖拽
                    //console.log("中间视图往左侧树拖拽")
                    control.dropViewItem(view.rootIndex)
                    
                    return
                }
                
                if(drop.hasUrls)
                {
                    //外部资源往树拖拽
                    //console.log("外部资源往树拖拽")
                    //console.log(drop.urls)
                    control.dropUrlList(view.rootIndex ,drop.urls);
                    return
                }
            }
            z:-100
        }
        
        
        onClickOther:
        {
            //console.log("onClickOther");
            disrenameItem(sel.currentIndex)
            clearSelect()
        }
        
        onDoubleClicked: {
            //console.log("treeview onDoubleClicked:" + index)
            
            if(!is_branch_hover)
            {
                if(control.getDoubleClickstate()===0)
                {
                    renameItem(index)
                }
                if(control.getDoubleClickstate()===1)
                {
                    if(view.isExpanded(index))
                    {
                        view.collapse(index)
                    }
                    else
                    {
                        view.expand(index)
                    }
                    view.repaintLine()
                }
            }
        }
        
        onClicked: {
            //console.log("onClicked treeview-----------------")
            //tree_view.dragItemIndex = index
            hover_index = undefined
            //control.selectMoreIndex(sel.selectedIndexes)
        }
        

        onSigndrag:
        {
            tree_view.dragItemIndex = index
        }
        
        onMouseReleased:{
            tree_view.dragItemIndex = undefined
        }
        onModelReseted:{
            menu_control.setIs_sort(false)
            control.RestoreExpandState();
        }
        
    }


    ScrollBar {
        id: verticalBar
        hoverEnabled: true
        active: hovered || pressed
        orientation: Qt.Vertical
        size: view.height / view.flickableItem.contentHeight
        visible:  !(view.height / view.flickableItem.contentHeight>1)
        //width: visible ? 8: 0

        minimumSize:0.6
        width:
        {
            if(visible)
            {
                return verticalBar.pressed
                        ?10
                        :verticalBar.hovered
                          ?10
                          :6
            }

            return 0
        }


        height: view.height
        anchors.top: view.top
        anchors.right: parent.right
        anchors.rightMargin: 2
        policy: ScrollBar.AsNeeded


        //滑块样式
        contentItem: Rectangle {
            implicitWidth: verticalBar.interactive ? 4 : 2
            implicitHeight: verticalBar.interactive ?4 : 2
            
    
            radius: 2.5
            color: verticalBar.pressed
                   ?handlePressColor
                   :verticalBar.hovered
                     ?handleHoverColor
                     :handleNormalColor

            //opacity:(verticalBar.policy === ScrollBar.AlwaysOn || verticalBar.size < 1.0)?1.0:0.0
        }



        onPositionChanged: {
            view.flickableItem.contentY = position * (view.flickableItem.contentHeight)
        }
    }
    Connections {
        target: view.flickableItem
        function onContentYChanged() {
//            if(model_update)
//            {
//                //view.visible = !view.visible
//                time_update_view_contony.running = true
//                model_update =false
//            }
            verticalBar.position = view.flickableItem.contentY /
                    view.flickableItem.contentHeight
        }
    }
    
    Component {
        id: item_delegate
        Item {
            id: treeDelegate
            property bool bDrop: false
            //property bool isHover: false
            property bool drop_active: false

            Canvas{
                id: _cv_Line
                x: -(1 + index_data.depth) * 16
                width:(1 + index_data.depth) * 16;
                height: parent.height+2
                y:-1
                z:100
                visible: true
                onPaint: {
                    if(index_data.depth !== 0 && index_data.index.valid){
                        var ctx = getContext("2d");
                        ctx.strokeStyle = line_color //color of line
                        ctx.lineWidth = 1
                        ctx.translate(0.5, 0.5)
                        var x_h, y_h, x_v, y_v

                        //Draw horizontal line
                        if(index_data.hasChildren){
                            x_h = objPrivate.mBranchWidth
                        }
                        else{
                            x_h = objPrivate.mBranchWidth/2
                        }
                        x_h += index_data.depth * objPrivate.mBranchWidth
                        y_h = _cv_Line.height/2
                        
                        if(!index_data.hasChildren){
                              drawHorizontalLine(ctx, x_h-2, y_h, _cv_Line.width)
                        }


                        var ret = control.getDrawLineList(index_data.index)
                        //Draw vertical line
                        for(var i = 0; i <= index_data.depth; i++) {
                            if(i === 0 && !mShowRootLines) {
                                continue;
                            }
                            
                            if(i !== index_data.depth){
                                if(!ret[i]){
                                   continue;
                                }

                            }

                            x_v = objPrivate.mBranchWidth/2 + i * objPrivate.mBranchWidth
                            if(index_data.hasChildren && (index_data.row !== 0) && i === index_data.depth) {
                                y_v = 0
                                
                                if(index_data.isExpanded || !index_data.hasSibling){
                                    drawVerticalLine(ctx, x_v-2, y_v,  _cv_Line.height/2)
                                }
                                else{
                                     drawVerticalLine(ctx, x_v-2, y_v,  _cv_Line.height)
                                }
                                
                                //y_v =  (_cv_Line.height + objPrivate.mBranchWidth)/2
                                //drawVerticalLine(ctx, x_v, y_v, _cv_Line.height)
                            } else {
                                if(index_data.hasChildren && index_data.row === 0){
                                    y_v = (_cv_Line.height + objPrivate.mBranchWidth)/2
                                }
                                else {
                                    y_v = 0
                                }
                                var yMax
                                if(index_data.hasSibling || i !== index_data.depth){
                                    yMax = _cv_Line.height
                                }
                                else {
                                    yMax = _cv_Line.height/2
                                }
                                drawVerticalLine(ctx, x_v-2, y_v, yMax)
                            }
                        }
                    }
                }
                //draw horizontal dot line
                function drawHorizontalLine(_ctx, _x, _y, _xMax){
                    _ctx.moveTo(_x, _y)
                    _ctx.lineTo(_xMax, _y)
                    _ctx.stroke()
                }

                //draw vertical dot line
                function drawVerticalLine(_ctx, _x, _y, _yMax){
                    _ctx.moveTo(_x, _y)
                    _ctx.lineTo(_x, _yMax)
                    _ctx.stroke()
                }

            }

            DropArea {
                z:100
                id: drop_area_top
                anchors
                {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                }

                Rectangle{
                    id: top_rect
                    height: 2
                    anchors.left: parent.left
                    anchors.right: parent.right
                    
                    anchors.top:  parent.top
                    color: brand_color
                    visible: false
                }

                height:drop_area_height
                keys: [ "BFTreeMimeData"]
                onDropped: {
                    tree_view.dragItemIndex = undefined
                    top_rect.visible =false
                    //console.log("左侧树内部拖拽   ------- 上部")

                    //console.log(file_name)

                    if(drop.getDataAsString("BFTreeMimeData"))
                    {
                        control.dropTreeItem(sel.selectedIndexes,index_data.index,1)
                        return
                    }
                }

                onEntered: {
                    hover_index = index_data.index
                    top_rect.visible =true
                }


                onExited: {
                    top_rect.visible =false
                }
            }

            DropArea {
                z:100
                id: drop_area_bottom
                anchors
                {
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                }

                Rectangle{
                    height: 2
                    id: bottom_rect
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom:  parent.bottom
                    color: brand_color
                    visible: false
                }

                height: drop_area_height
                keys: [ "BFTreeMimeData"]
                onDropped: {
                    tree_view.dragItemIndex = undefined
                    bottom_rect.visible =false
                    //console.log("左侧树内部拖拽   ------- 下部")
                    //console.log(file_name)
                    if(drop.getDataAsString("BFTreeMimeData"))
                    {
                        control.dropTreeItem(sel.selectedIndexes,index_data.index,2)
                        return
                    }
                }

                onEntered: {
                    bottom_rect.visible =true
                    hover_index = index_data.index
                }
                onExited: {
                    bottom_rect.visible =false
                }
            }

            DropArea {
                z:100
                id: drop_area_content
                anchors
                {
                    top: drop_area_top.bottom
                    bottom: drop_area_bottom.top
                    left: parent.left
                    right: parent.right
                }

                keys: [ "BFTreeMimeData","text/uri-list","FileFromView","chromium/x-web-custom-data"]
                onDropped: {
                    if(tree_view.dragItemIndex ===index_data.index)
                    {
                        //console.log("左侧树自己拖拽到自己不执行拖拽")
                        tree_view.dragItemIndex = undefined
                        bDrop = false
                        return ;
                    }


                    tree_view.dragItemIndex = undefined
                    bDrop = false

                    var dropformats = drop.formats;
                    if(dropformats.indexOf("chromium/x-web-custom-data") !== -1)
                    {
                        control.dropWebData(index_data.index);
                        drop.accept();
                        return
                    }


                    //console.log(file_name)
                    if(drop.getDataAsString("BFTreeMimeData"))
                    {
                        //console.log("左侧树内部拖拽")
                        control.dropTreeItem(sel.selectedIndexes,index_data.index,0)
                        return
                    }


                    if(drop.source && drop.source.objectName=="BFBaseView")
                    {
                        //外部资源往树拖拽
                        //console.log("中间视图往左侧树拖拽")
                        control.dropViewItem(index_data.index)
                        return
                    }

                    if(drop.hasUrls)
                    {
                        //外部资源往树拖拽
                        //console.log("外部资源往树拖拽")
                        //console.log(drop.urls)
                        control.dropUrlList(index_data.index,drop.urls);
                        return
                    }
                }

                onEntered: {

                    //console.log("左侧树内部拖拽   -------中间")
                    hover_index = index_data.index
                    expand_item = index_data.index
                    expend_item_timer.running=true
                    bDrop = true
                    
                    var yy =mapToItem(view,drag.x,drag.y).y
                    
                    //console.log("----------"+"    "+yy +"    " +view.height +"    "+ index_data.row)
                    
                    if(yy>=0 && yy<=30)
                    {
                        //向上滚动
                    
                        scroll_row =0
                        auto_scroll.running=true
                    }
                    
                    
                    if(yy<=view.height &&  yy> (view.height-30))
                    {
                        //向下滚动
                       scroll_row=1
                       auto_scroll.running=true

                    }
                }

                onExited: {
                    expand_item = undefined
                    bDrop = false
                }
            }

            Rectangle {
                id: dragRect
                anchors.fill:parent
                radius:6
                height:30
                color:
                {
                   
                    
                    
                    if(tree_view.isFolderSelected(index_data.index))
                    {
                        return ue_click_color;
                    }
                    else if(hover_index ===index_data.index)
                    {
                        if(bDrop){
                            return "#4D4F7DFF"
                        }

                        return ue_hover_color
                    }
                    else
                    {
                        return background_color
                    }
                }

                opacity:
                {
                    var state =control.getTreeItemHideOrShow(index_data.index)+control.update_state
                    if(state)
                    {
                        return  0.3
                    }

                    return 1
                }


                border.color:
                {
                    if(right_index ===(index_data.index))
                    {
                         return font_number_color
                    }

                    return bDrop?brand_color:background_color
                }


                Image {
                    id: folder_logo
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    source: 
                    {
                        var state =control.isBrowseFloder(index_data.index)+menu_control.update_browser
                        if(state)
                        {
                            return "file:///"+skin_model.GetResourceAbsolutePath("icon_browser_normal.svg")
                        }
                        
                        var cloud_state =control.isCloudFloder(index_data.index)+menu_control.update_browser
                        if(cloud_state)
                        {
                            return "file:///"+skin_model.GetResourceAbsolutePath("icon_cloud_local_normal.svg")
                        }
                        return  !index_data.isExpanded?"file:///"+skin_model.GetResourceAbsolutePath("icon_folder_close_normal.svg"):"file:///"+skin_model.GetResourceAbsolutePath("icon_folder_open_normal.svg")
                    }

                    width:18
                    height: 18
                    //smooth:true
                    //mipmap:true
                    
                    layer.enabled:true
                    layer.effect:ColorOverlay{
                        anchors.fill: folder_logo
                        source: folder_logo
                        color: {
                                if(control.getFolderColorIndex(index_data.index)===0 && right_index!==index_data.index && sel.selectedIndexes.length===1)
                                {
                                    if(hover_index ===index_data.index )
                                        return font_color
                                    if(tree_view.isFolderSelected(index_data.index))
                                        return common_major_color
                                }
                                
                                return control.GetFolderColor(index_data.index)+menu_control.folder_color_change
                            }

                        cached: false
                    }
                }

                Text {
                    id: label
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: folder_logo.right
                    anchors.leftMargin: 5
                    text:file_name
                    anchors.right: number_label.left
                    elide: Text.ElideRight
                    color:
                    {
                        if(hover_index ===index_data.index)
                            return font_color

                        if(tree_view.isFolderSelected(index_data.index))
                            return common_major_color

                        return font_color
                    }

                    font.pixelSize: 13
                }

                Text {
                    id: number_label
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    width:50
                    color:
                    {
                        if(hover_index ===index_data.index)
                            return font_number_color
                        
                        if(tree_view.isFolderSelected(index_data.index))
                            return font_color
                        
                        return font_number_color
                    }
                    text: number_count

                    font.pixelSize: 13
                    font.family: skin_model.font_family
                    horizontalAlignment: Text.AlignRight
                }

                Rectangle {
                    id: text_edit_rect
                    anchors.left: folder_logo.right
                    anchors.right: parent.right
                    anchors.rightMargin: 5
                    anchors.top: parent.top
                    anchors.topMargin:4
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin:4
                    color: "#FCFCFC"
                    border.color: background_color
                    visible: false
                    radius: 4

                    TextInput {
                        id: textEdit
                        leftPadding: 5
                        //topPadding: 5
                        //bottomPadding: 3
                        
                        anchors.centerIn: parent
                        width:parent.width
                        height: 16
                        text: file_name
                        autoScroll:true
                        clip:true
                        font.pixelSize: 13
                        selectedTextColor:"#FCFCFC"
                        selectionColor: brand_color
                        selectByMouse:true
                        onEditingFinished: {

                            if(text_edit_rect.visible===true)
                            {
                                control.EditFinishedItem(view.currentIndex,text)
                            }

                            text_edit_rect.setRenameFolderState(false)
                        }

                        onFocusChanged:
                        {
                            if(textEdit.focus===true)
                            {
                                textEdit.text = file_name
                                textEdit.selectAll()
                            }
                        }

                    }

                    function setRenameFolderState(active) {
                        //console.log("treeview setRenameFolderState:" + active)
                        if(active) {
                            text_edit_rect.visible = true
                            textEdit.visible=true
                            //textEdit.focus =true
                            label.visible=false
                        }
                        else {
                            text_edit_rect.visible =false
                            textEdit.visible=false
                            textEdit.focus =false
                            label.visible=true
                        }
                    }

                    Connections {
                        target: view
                        function onRenameItem(index)
                        {
                            if(index === index_data.index) {
                                //console.log("treeview onRenameItem")
                                textEdit.forceActiveFocus()
                                text_edit_rect.setRenameFolderState(true)
                            }
                        }
                        
                        
                        function onRepaintLine()
                        {
                            var ctx = _cv_Line.getContext("2d")
                            ctx.reset()
                            _cv_Line.requestPaint()
                        }

                        function onDisrenameItem(index)
                        {
                            
                            if(index === index_data.index) {

                                //console.log(index_data.value)
                                textEdit.focus=false
                                if(text_edit_rect.visible===true)
                                {
                                    control.EditFinishedItem(view.currentIndex,text)
                                }

                                text_edit_rect.setRenameFolderState(false)
                            }
                        }
                    }
                }

                MouseArea{
                    z:-100
                    id:rowMouseArea
                    
                    anchors.fill: parent
                    hoverEnabled: true
                    //property  bool hovered : false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    propagateComposedEvents: true
                    drag.target: dragRect
                    drag.onActiveChanged: {

                        //console.log("drag.onActiveChanged")
                        if (rowMouseArea.drag.active) {
                            dragIndex = index_data.index;
                        }
                        dragRect.Drag.drop();
                    }

                    onPressed: {
                        //console.log("-------onPressed")
                        hover_index = undefined
                        if(mouse.button == Qt.RightButton)
                        {
                            right_index = index_data.index
                            signRightPress();
                            var x =mapToGlobal(rowMouseArea.mouseX,rowMouseArea.mouseY).x
                            var y =mapToGlobal(rowMouseArea.mouseX,rowMouseArea.mouseY).y
                            
                            var indexs =sel.selectedIndexes;
                            
                            
                            var select_indexs=[]
                            for(var i = 0; i < indexs.length;i++){
                                var ret  =indexs[i];
            
                                if(ret ===index_data.index)
                                {
                                    select_indexs =sel.selectedIndexes
                                    break;
                                }
                            }
                            
                            if(select_indexs.length===0)
                                select_indexs.push(index_data.index)

                            menu_control.mousePress(x,y,select_indexs)
                            mouse.accepted = true

                        }
                        else {
                             right_index = undefined
                            mouse.accepted = false
                        }
                    


                    }
                    onClicked:
                    {
                        hover_index = undefined
                        mouse.accepted = false;
                    }


                    onDoubleClicked: {
                    }



                    onEntered:
                    {
                        hover_index = index_data.index
                    }

                    onExited:
                    {
                        hover_index = undefined
                    }
                }

                Connections
                {
                    target: menu_control
                    function onSigQmlReName( folder_id)
                    {
                        //console.log("qml rename")
                        if(folder_id === control.GetFolderItemId(index_data.index))
                        {
                            view.renameItem(index_data.index)
                        }
                    }


                    function onSigMenuClose()
                    {
                        view.focus=true
                        right_index=undefined
                    }
                }
            }

            Drag.active:
            {
                if(index_data.index !==undefined && tree_view.dragItemIndex=== index_data.index && index_data.index.valid)
                    return true
                return false
            }
            Drag.supportedActions: Qt.CopyAction
            Drag.hotSpot.x: dragRect.width / 2
            Drag.hotSpot.y: dragRect.height / 2
            Drag.mimeData: {
                "BFTreeMimeData":  "BFTreeMimeData",
                "text/html": setDragBrowseData()}
            Drag.dragType: Drag.Automatic
            //Drag.imageSource: "file:///1.jpg"

            function setDragBrowseData(){
                 var indexs =sel.selectedIndexes;

                return control.getWebMimeData(indexs);
            }
        }
    }

    Component {
        id: treeViewStyle
        TreeViewStyle {
            padding.left: 2
            padding.right: 2
            indentation: 16  //节点间缩进
            branchDelegate:
                Rectangle
            {
                id:branch
                width: 16
                height:16
                //visible:styleData.row!==0
                color: background_color
                Image{//节点展开标记图
                    id:image
                    anchors.rightMargin: 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    
                    source:
                    {
                        if(!styleData.isExpanded)
                        {
                            image.press
                            if(styleData.index ===branch_hover_current_item&& is_branch_hover)
                            {
                                return "file:///"+skin_model.GetResourceAbsolutePath("icon_close_hover.svg")
                            }

                            return "file:///"+skin_model.GetResourceAbsolutePath("icon_close_normal.svg")
                        }
                        else
                        {

                            if( styleData.index ===branch_hover_current_item && is_branch_hover)
                            {
                                return "file:///"+skin_model.GetResourceAbsolutePath("icon_open_hover.svg")
                            }

                            return "file:///"+skin_model.GetResourceAbsolutePath("icon_open_normal.svg")
                        }
                    }
                    
                    //smooth:true
                    //mipmap:true
                    
                    width:8
                    height: 8

                }

                MouseArea
                {
                    id: branch_mouse_area
                    anchors.fill: parent
                    hoverEnabled: true

                    onEntered:
                    {
                        is_branch_hover=true
                        branch_hover_current_item =styleData.index
                    }


                    onExited:
                    {
                        branch_hover_current_item =undefined
                        is_branch_hover = false
                    }

                    onClicked:
                    {
                        if(styleData.isExpanded)
                        {
                            view.collapse(styleData.index)
                        }
                        else
                        {
                            view.expand(styleData.index)
                        }
                        
                        view.repaintLine()
                    }
                }
            }


            rowDelegate: Rectangle {
                color: background_color   //选中节点后的颜色和背景色
                height: 30
                radius: styleData.selected ? 10 : 0
            }

            frame: Rectangle { color: "transparent" }　//边框
            incrementControl: Rectangle { color: "transparent" }　//滑块
            decrementControl: Rectangle { color: "transparent" }  //滑块
            handle: Rectangle {   //中间的滑动块
                implicitWidth: 0
                Rectangle {
                    radius: 2.5
                    color: "transparent"
                    anchors.fill: parent
                }
            }
            //背景
            scrollBarBackground:
                Rectangle {
                color: "transparent"
                implicitWidth: 0
            }
        }
    }
}
