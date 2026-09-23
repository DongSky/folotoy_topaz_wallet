package com.folotoy.passport.wallet.render;

import static org.junit.Assert.*;
import org.junit.Test;

public class PageRulesTest {
    @Test public void qrUsesWholeModulesAndFourModuleQuietZone() {
        PageRules.QrLayout l = PageRules.qrLayout(29, 196);
        assertEquals(5, l.scale()); assertEquals(185, l.pixels()); assertEquals(4, l.quietModules());
    }
    @Test(expected=IllegalArgumentException.class) public void qrTooDenseIsRejectedVisibly() {
        PageRules.qrLayout(213, 196);
    }
    @Test(expected=IllegalArgumentException.class) public void onePixelModulesAreRejected() {
        PageRules.qrLayout(85, 184);
    }
}
