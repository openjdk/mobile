/*
 * Copyright (c) 2017, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package sanity;

import jdk.test.lib.process.ProcessTools;

import java.util.List;

/*
 * @test
 * @summary make sure various basic java options work
 * @library /test/lib
 *
 * @run driver sanity.BasicVMTest
 */
public class BasicVMTest {
    public static void main(String[] args) throws Exception {
        List<String> flags = List.of(
                "-version",
                "-Xinternalversion",
                "-X",
                "-help");
        for (String flag : flags) {
            ProcessTools.executeTestJava(flag)
                        .shouldHaveExitValue(0);
        }
    }
}
