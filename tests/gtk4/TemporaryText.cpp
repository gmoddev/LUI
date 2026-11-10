#include <gtk/gtk.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool Value,const char* Name) {if(!Value)throw std::runtime_error(Name);}

static std::string Text(GtkTextBuffer* Buffer) {
    GtkTextIter First,Last;
    gtk_text_buffer_get_bounds(Buffer,&First,&Last);
    char* Bytes=gtk_text_buffer_get_text(Buffer,&First,&Last,FALSE);
    Require(Bytes!=nullptr,"ReadText");
    std::string Value(Bytes);
    g_free(Bytes);
    return Value;
}

static void Style(GtkTextBuffer* Buffer,GtkTextTag* Tag,int Start,int End) {
    GtkTextIter First,Last;
    gtk_text_buffer_get_iter_at_offset(Buffer,&First,Start);
    gtk_text_buffer_get_iter_at_offset(Buffer,&Last,End);
    gtk_text_buffer_apply_tag(Buffer,Tag,&First,&Last);
}

int main() {
    try {
        gtk_init();
        GtkTextBuffer* Buffer=gtk_text_buffer_new(nullptr);
        Require(Buffer!=nullptr,"CreateBuffer");
        gtk_text_buffer_set_enable_undo(Buffer,TRUE);
        GtkTextTag* Tag=gtk_text_buffer_create_tag(Buffer,"lui-syntax","foreground","#aa33ff",nullptr);
        Require(Tag!=nullptr,"CreateColorTag");
        const std::string Source=u8"local Value = '😀'\nreturn Value";
        gtk_text_buffer_set_text(Buffer,Source.c_str(),static_cast<int>(Source.size()));
        // Fixture setup only. Styling never clears or suspends native Undo.
        gtk_text_buffer_begin_irreversible_action(Buffer);
        gtk_text_buffer_end_irreversible_action(Buffer);
        GtkTextIter Insert;
        gtk_text_buffer_get_start_iter(Buffer,&Insert);
        gtk_text_buffer_begin_user_action(Buffer);
        gtk_text_buffer_insert(Buffer,&Insert,"x",1);
        gtk_text_buffer_end_user_action(Buffer);
        const std::string Edited=Text(Buffer);
        Require(Edited=="x"+Source && gtk_text_buffer_get_can_undo(Buffer),"NativeUndoableEdit");
        GtkTextIter SelectionStart,SelectionEnd;
        gtk_text_buffer_get_iter_at_offset(Buffer,&SelectionStart,2);
        gtk_text_buffer_get_iter_at_offset(Buffer,&SelectionEnd,8);
        gtk_text_buffer_select_range(Buffer,&SelectionStart,&SelectionEnd);
        gtk_text_buffer_set_modified(Buffer,FALSE);
        for(int Index=0;Index<8;++Index)Style(Buffer,Tag,0,5);
        GtkTextIter AfterStart,AfterEnd;
        Require(gtk_text_buffer_get_selection_bounds(Buffer,&AfterStart,&AfterEnd),"SelectionPresent");
        Require(gtk_text_iter_get_offset(&AfterStart)==2 && gtk_text_iter_get_offset(&AfterEnd)==8,
            "StylePreservesSelection");
        Require(Text(Buffer)==Edited && !gtk_text_buffer_get_modified(Buffer),
            "StylePreservesTextAndModifiedFlag");
        Require(gtk_text_buffer_get_can_undo(Buffer),"StylePreservesUndo");
        GtkTextIter Styled;
        gtk_text_buffer_get_iter_at_offset(Buffer,&Styled,2);
        GSList* Tags=gtk_text_iter_get_tags(&Styled);
        Require(g_slist_find(Tags,Tag)!=nullptr,"NativeTagApplied");
        g_slist_free(Tags);
        gtk_text_buffer_undo(Buffer);
        Require(Text(Buffer)==Source && gtk_text_buffer_get_can_redo(Buffer),"OneUndoRestoresSource");
        Style(Buffer,Tag,0,5);
        Require(gtk_text_buffer_get_can_redo(Buffer),"StylePreservesRedo");
        gtk_text_buffer_redo(Buffer);
        Require(Text(Buffer)==Edited,"OneRedoRestoresSource");
        std::printf("[LUI:GtkTextProof] PASS Text Selection Modified Undo Redo Unicode LF Tag\n");

        std::string Large;
        while(Large.size()<32000)Large+="local Value = 'text' -- comment\n";
        gtk_text_buffer_set_text(Buffer,Large.c_str(),static_cast<int>(Large.size()));
        const std::string LargeBefore=Text(Buffer);
        std::vector<double> Samples;
        for(int Sample=0;Sample<136;++Sample) {
            auto Started=std::chrono::steady_clock::now();
            GtkTextIter First,Last;
            gtk_text_buffer_get_bounds(Buffer,&First,&Last);
            gtk_text_buffer_remove_all_tags(Buffer,&First,&Last);
            for(int Span=0;Span<128;++Span)Style(Buffer,Tag,Span*32,Span*32+5);
            if(Sample>=8)Samples.push_back(std::chrono::duration<double,std::micro>(
                std::chrono::steady_clock::now()-Started).count());
        }
        Require(Text(Buffer)==LargeBefore,"LargeExactSource");
        std::sort(Samples.begin(),Samples.end());
        std::printf("[LUI:GtkTextProof] NativeTagBatchUs P50=%.3f P95=%.3f P99=%.3f Spans=128 Samples=128 UTF8Bytes=%zu DisplayLatency=NOT_MEASURED Allocations=NOT_MEASURED\n",
            Samples[63],Samples[121],Samples[126],LargeBefore.size());
        g_object_unref(Buffer);
        return 0;
    } catch(const std::exception& Error) {
        std::fprintf(stderr,"[LUI:GtkTextProof] FAIL %s\n",Error.what());
        return 1;
    }
}
