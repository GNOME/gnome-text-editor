# Recoloring Text Editor

The goal is to recolor Text Editor based on the style-scheme that is selected.
The following example from Alexander Mikhaylenko shows how we can do that for the solarized theme.

```css
@define-color card_fg_color @window_fg_color;
@define-color headerbar_fg_color @window_fg_color;
@define-color headerbar_border_color @window_fg_color;
@define-color popover_fg_color @window_fg_color;
@define-color dark_fill_bg_color @headerbar_bg_color;
@define-color view_bg_color @card_bg_color;
@define-color view_fg_color @window_fg_color;
/* solarized light */
@define-color window_bg_color #fdf6e3;
@define-color window_fg_color #073642;
@define-color headerbar_bg_color #eee8d5;
@define-color popover_bg_color mix(@window_bg_color, white, .1);
@define-color card_bg_color alpha(white, .65);
@define-color accent_bg_color #657b83;
@define-color accent_color #586e75;
```
## TODO

 * Make sure that we have metadata available for the style-schemes to simplify this.
   * Is the style-scheme light or dark?
   * What colors should we use for styling?
 * Generate CSS based on style-scheme change.
 * Make sure we still track light/dark modes correctly.

