/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2013 - Raw Material Software Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

#if JUCE_ENABLE_LIVE_CONSTANT_EDITOR

namespace LiveConstantEditor
{

//==============================================================================
class AllComponentRepainter  : private Timer,
                               private DeletedAtShutdown
{
public:
    AllComponentRepainter() {}

    static AllComponentRepainter& getInstance()
    {
        static AllComponentRepainter* instance = new AllComponentRepainter();
        return *instance;
    }

    void trigger()
    {
        startTimer (70);
    }

private:
    void timerCallback() override
    {
        stopTimer();

        for (int i = TopLevelWindow::getNumTopLevelWindows(); --i >= 0;)
            if (Component* c = TopLevelWindow::getTopLevelWindow(i))
                repaintAndResizeAllComps (c);
    }

    static void repaintAndResizeAllComps (Component::SafePointer<Component> c)
    {
        for (int i = c->getNumChildComponents(); --i >= 0;)
        {
            if (c != nullptr)
            {
                if (Component* child = c->getChildComponent(i))
                {
                    child->repaint();
                    child->resized();
                }
            }
        }
    }
};

//==============================================================================
int64 parseInt (String s)
{
    s = s.retainCharacters ("0123456789abcdefABCDEFx");

    if (s.startsWith ("0x"))
        return s.substring(2).getHexValue64();

    return s.getLargeIntValue();
}

double parseDouble (const String& s)
{
    return s.retainCharacters ("0123456789.eE-").getDoubleValue();
}

String hexString (int v)    { return "0x" + String::toHexString (v); }
String hexString (int64 v)  { return "0x" + String::toHexString (v); }

//==============================================================================
LiveValueBase::LiveValueBase (const char* file, int line)
    : sourceFile (file), sourceLine (line)
{
    name = File (sourceFile).getFileName() + " : " + String (sourceLine);
}

LiveValueBase::~LiveValueBase()
{
}


//==============================================================================
LivePropertyEditorBase::LivePropertyEditorBase (LiveValueBase& v, CodeDocument& d)
    : value (v), document (d), sourceEditor (document, &tokeniser)
{
    setSize (600, 100);

    addAndMakeVisible (&name);
    addAndMakeVisible (&valueEditor);
    addAndMakeVisible (&sourceEditor);
    sourceEditor.setWantsKeyboardFocus (false);
    sourceEditor.setEnabled (false);

    findOriginalValueInCode();
    selectOriginalValue();

    name.setFont (13.0f);
    name.setText (v.name, dontSendNotification);
    valueEditor.setText (v.getStringValue(), dontSendNotification);
    valueEditor.addListener (this);
}

void LivePropertyEditorBase::paint (Graphics& g)
{
    g.setColour (Colours::white);
    g.fillRect (getLocalBounds().removeFromBottom (1));
}

void LivePropertyEditorBase::resized()
{
    Rectangle<int> r (getLocalBounds().reduced (0, 3).withTrimmedBottom (1));

    Rectangle<int> left (r.removeFromLeft (jmax (200, r.getWidth() / 3)));

    name.setBounds (left.removeFromTop (25));
    valueEditor.setBounds (left.removeFromTop (25));
    left.removeFromTop (2);

    if (customComp != nullptr)
        customComp->setBounds (left);

    r.removeFromLeft (4);
    sourceEditor.setBounds (r);
}

void LivePropertyEditorBase::textEditorTextChanged (TextEditor&)
{
    applyNewValue (valueEditor.getText());
}

void LivePropertyEditorBase::applyNewValue (const String& s)
{
    value.setStringValue (s);

    document.replaceSection (valueStart.getPosition(), valueEnd.getPosition(), value.getCodeValue());
    document.clearUndoHistory();
    selectOriginalValue();

    valueEditor.setText (s, dontSendNotification);
    AllComponentRepainter::getInstance().trigger();
}

void LivePropertyEditorBase::selectOriginalValue()
{
    sourceEditor.selectRegion (valueStart, valueEnd);
}

void LivePropertyEditorBase::findOriginalValueInCode()
{
    CodeDocument::Position pos (document, value.sourceLine, 0);
    String line (pos.getLineText());
    String::CharPointerType p (line.getCharPointer());

    p = CharacterFunctions::find (p, CharPointer_ASCII ("JUCE_LIVE_CONSTANT"));

    if (p.isEmpty())
    {
        // Not sure how this would happen - some kind of mix-up between source code and line numbers..
        jassertfalse;
        return;
    }

    p += (int) (sizeof ("JUCE_LIVE_CONSTANT") - 1);
    p = p.findEndOfWhitespace();

    if (! CharacterFunctions::find (p, CharPointer_ASCII ("JUCE_LIVE_CONSTANT")).isEmpty())
    {
        // Aargh! You've added two JUCE_LIVE_CONSTANT macros on the same line!
        // They're identified by their line number, so you must make sure each
        // one goes on a separate line!
        jassertfalse;
    }

    if (p.getAndAdvance() == '(')
    {
        String::CharPointerType start (p), end (p);

        int depth = 1;

        while (! end.isEmpty())
        {
            const juce_wchar c = end.getAndAdvance();

            if (c == '(')  ++depth;
            if (c == ')')  --depth;

            if (depth == 0)
            {
                --end;
                break;
            }
        }

        if (end > start)
        {
            valueStart = CodeDocument::Position (document, value.sourceLine, (int) (start - line.getCharPointer()));
            valueEnd   = CodeDocument::Position (document, value.sourceLine, (int) (end   - line.getCharPointer()));

            valueStart.setPositionMaintained (true);
            valueEnd.setPositionMaintained (true);
        }
    }
}

//==============================================================================
class ValueListHolderComponent  : public Component
{
public:
    ValueListHolderComponent (ValueList& l) : valueList (l)
    {
        setVisible (true);
    }

    void addItem (int width, LiveValueBase& v, CodeDocument& doc)
    {
        addAndMakeVisible (editors.add (v.createPropertyComponent (doc)));
        layout (width);
    }

    void layout (int width)
    {
        setSize (width, editors.size() * itemHeight);
        resized();
    }

    void resized() override
    {
        Rectangle<int> r (getLocalBounds());

        for (int i = 0; i < editors.size(); ++i)
            editors.getUnchecked(i)->setBounds (r.removeFromTop (itemHeight));
    }

    enum { itemHeight = 120 };

    ValueList& valueList;
    OwnedArray<LivePropertyEditorBase> editors;
};

//==============================================================================
class ValueList::EditorWindow  : public DocumentWindow,
                                 private DeletedAtShutdown
{
public:
    EditorWindow (ValueList& list)
        : DocumentWindow ("Live Values", Colours::lightgrey, DocumentWindow::closeButton)
    {
        setLookAndFeel (&lookAndFeel);
        setUsingNativeTitleBar (true);

        viewport.setViewedComponent (new ValueListHolderComponent (list), true);
        viewport.setSize (700, 600);
        viewport.setScrollBarsShown (true, false);

        setContentNonOwned (&viewport, true);
        setResizable (true, false);
        setResizeLimits (500, 400, 10000, 10000);
        setVisible (true);
        centreWithSize (getWidth(), getHeight());
    }

    void closeButtonPressed() override
    {
        setVisible (false);
    }

    void addItem (LiveValueBase& v, CodeDocument& doc)
    {
        if (ValueListHolderComponent* l = dynamic_cast<ValueListHolderComponent*> (viewport.getViewedComponent()))
            l->addItem (viewport.getMaximumVisibleWidth(), v, doc);
    }

    void resized() override
    {
        DocumentWindow::resized();

        if (ValueListHolderComponent* l = dynamic_cast<ValueListHolderComponent*> (viewport.getViewedComponent()))
            l->layout (viewport.getMaximumVisibleWidth());
    }

    Viewport viewport;
    LookAndFeel_V3 lookAndFeel;
};


struct ColourEditorComp  : public Component,
                           private ChangeListener
{
    ColourEditorComp (LivePropertyEditorBase& e)  : editor (e)
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    Colour getColour() const
    {
        return Colour ((int) parseInt (editor.value.getStringValue()));
    }

    void paint (Graphics& g) override
    {
        g.fillCheckerBoard (getLocalBounds(), 6, 6,
                            Colour (0xffdddddd).overlaidWith (getColour()),
                            Colour (0xffffffff).overlaidWith (getColour()));
    }

    void mouseDown (const MouseEvent&) override
    {
        ColourSelector* colourSelector = new ColourSelector();
        colourSelector->setName ("Colour");
        colourSelector->setCurrentColour (getColour());
        colourSelector->addChangeListener (this);
        colourSelector->setColour (ColourSelector::backgroundColourId, Colours::transparentBlack);
        colourSelector->setSize (300, 400);

        CallOutBox::launchAsynchronously (colourSelector, getScreenBounds(), nullptr);
    }

    void changeListenerCallback (ChangeBroadcaster* source) override
    {
        if (ColourSelector* cs = dynamic_cast<ColourSelector*> (source))
            editor.applyNewValue (getAsString (cs->getCurrentColour()));

        repaint();
    }

    LivePropertyEditorBase& editor;
};

Component* createColourEditor (LivePropertyEditorBase& editor)
{
    return new ColourEditorComp (editor);
}

//==============================================================================
class SliderComp   : public Component,
                     private Slider::Listener
{
public:
    SliderComp (LivePropertyEditorBase& e, bool useFloat)
        : editor (e), isFloat (useFloat)
    {
        slider.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible (&slider);
        updateRange();
        slider.addListener (this);
    }

    void updateRange()
    {
        double v = isFloat ? parseDouble (editor.value.getStringValue())
                           : (double) parseInt (editor.value.getStringValue());

        double range = isFloat ? 10 : 100;

        slider.setRange (v - range, v + range);
        slider.setValue (v, dontSendNotification);
    }

private:
    LivePropertyEditorBase& editor;
    Slider slider;
    bool isFloat;

    void sliderValueChanged (Slider*)
    {
        editor.applyNewValue (isFloat ? getAsString ((double) slider.getValue())
                                      : getAsString ((int64)  slider.getValue()));

    }

    void sliderDragStarted (Slider*)  {}
    void sliderDragEnded (Slider*)    { updateRange(); }

    void resized()
    {
        slider.setBounds (getLocalBounds().removeFromTop (25));
    }
};


Component* createIntegerSlider (LivePropertyEditorBase& editor) { return new SliderComp (editor, false); }
Component* createFloatSlider   (LivePropertyEditorBase& editor) { return new SliderComp (editor, true);  }

//==============================================================================
ValueList::ValueList()
{
}

ValueList& ValueList::getInstance()
{
    static ValueList* i = new ValueList();
    return *i;
}

void ValueList::addValue (LiveValueBase* v)
{
    values.add (v);

    if (editorWindow == nullptr)
        editorWindow = new EditorWindow (*this);

    editorWindow->addItem (*v, getDocument (v->sourceFile));
    editorWindow->setVisible (true);
}

CodeDocument& ValueList::getDocument (const File& file)
{
    const int index = documentFiles.indexOf (file.getFullPathName());

    if (index >= 0)
        return *documents.getUnchecked (index);

    CodeDocument* doc = documents.add (new CodeDocument());
    documentFiles.add (file);
    doc->replaceAllContent (file.loadFileAsString());
    doc->clearUndoHistory();
    return *doc;
}

}

#endif