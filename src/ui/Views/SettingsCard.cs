using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace TailDrop.UI.Views;

/// <summary>
/// A row in Settings, like the cards in Windows 11 Settings: an icon, a
/// header and description on the left, a control (the content) on the right.
/// The template lives in App.xaml.
/// </summary>
public sealed partial class SettingsCard : ContentControl
{
    public static readonly DependencyProperty HeaderProperty =
        DependencyProperty.Register(nameof(Header), typeof(string), typeof(SettingsCard), new PropertyMetadata("", OnTextChanged));

    public static readonly DependencyProperty DescriptionProperty =
        DependencyProperty.Register(nameof(Description), typeof(string), typeof(SettingsCard), new PropertyMetadata("", OnTextChanged));

    public static readonly DependencyProperty GlyphProperty =
        DependencyProperty.Register(nameof(Glyph), typeof(string), typeof(SettingsCard), new PropertyMetadata("", OnTextChanged));

    private TextBlock? _description;
    private FontIcon? _icon;

    public string Header
    {
        get => (string)GetValue(HeaderProperty);
        set => SetValue(HeaderProperty, value);
    }

    public string Description
    {
        get => (string)GetValue(DescriptionProperty);
        set => SetValue(DescriptionProperty, value);
    }

    public string Glyph
    {
        get => (string)GetValue(GlyphProperty);
        set => SetValue(GlyphProperty, value);
    }

    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        _description = GetTemplateChild("DescriptionText") as TextBlock;
        _icon = GetTemplateChild("IconGlyph") as FontIcon;
        UpdateParts();
    }

    private static void OnTextChanged(DependencyObject d, DependencyPropertyChangedEventArgs e) => ((SettingsCard)d).UpdateParts();

    private void UpdateParts()
    {
        if (_description != null)
            _description.Visibility = string.IsNullOrEmpty(Description) ? Visibility.Collapsed : Visibility.Visible;
        if (_icon != null)
            _icon.Visibility = string.IsNullOrEmpty(Glyph) ? Visibility.Collapsed : Visibility.Visible;
    }
}
