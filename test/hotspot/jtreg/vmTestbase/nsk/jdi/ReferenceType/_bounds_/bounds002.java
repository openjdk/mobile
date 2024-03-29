/*
 * Copyright (c) 2002, 2024, Oracle and/or its affiliates. All rights reserved.
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

package nsk.jdi.ReferenceType._bounds_;

import nsk.share.*;
import nsk.share.jpda.*;
import nsk.share.jdi.*;

import com.sun.jdi.*;

import java.util.*;
import java.io.*;

/**
 * The test checks up the methods of com.sun.jdi.ReferenceType:
 *    - fieldByName(String)
 *    - methodsByName(String)
 *    - methodsByName(String, String)
 *    - getValue(Field)
 *    - getValues(List)
 *    - equals(Object)
 * These methods are invoked with null-value of parameter.
 * getValues(List) is invoked with List of zero-size too.
 */

public class bounds002 {

    private final static String prefix = "nsk.jdi.ReferenceType._bounds_.";
    private final static String debuggerName = prefix + "bounds002";
    private final static String debugeeName = debuggerName + "a";

    public final static String SGNL_READY = "ready";
    public final static String SGNL_QUIT = "quit";

    private static int exitStatus;
    private static Log log;
    private static Debugee debugee;

    private static void display(String msg) {
        log.display(msg);
    }

    private static void complain(String msg) {
        log.complain("debugger FAILURE> " + msg + "\n");
    }

    public static void main(String argv[]) {
        int result = run(argv,System.out);
        if (result != 0) {
            throw new RuntimeException("TEST FAILED with result " + result);
        }
    }

    public static int run(String argv[], PrintStream out) {

        exitStatus = Consts.TEST_PASSED;

        bounds002 thisTest = new bounds002();

        ArgumentHandler argHandler = new ArgumentHandler(argv);
        log = new Log(out, argHandler);

        debugee = Debugee.prepareDebugee(argHandler, log, debugeeName);

        thisTest.execTest();
        display("Test finished. exitStatus = " + exitStatus);

        return exitStatus;
    }

    private void execTest() {

        ReferenceType testedClass = debugee.classByName(debugeeName);

        display("\nTEST BEGINS");
        display("===========");

        Value retValue;

        Field field;
        display("fieldByName(null)");
        try {
            field = testedClass.fieldByName(null);
            if (field != null) {
                complain("wrong field: " + field);
                exitStatus = Consts.TEST_FAILED;
            } else {
                display("field: " + field);
            }
        } catch(Exception e) {
            complain("Unexpected " + e);
            exitStatus = Consts.TEST_FAILED;
        }
        display("");

        List methodList;
        display("methodsByName(null)");
        try {
            methodList = testedClass.methodsByName(null);
            if (methodList.size() != 0) {
                complain("wrong size of method list: " + methodList.size());
                exitStatus = Consts.TEST_FAILED;
            } else {
                display("size of method list: " + methodList.size());
            }
        } catch(Exception e) {
            complain("Unexpected " + e);
            exitStatus = Consts.TEST_FAILED;
        }
        display("");

        display("methodsByName(null, null)");
        try {
            methodList = testedClass.methodsByName(null, null);
            if (methodList.size() != 0) {
                complain("wrong size of method list: " + methodList.size());
                exitStatus = Consts.TEST_FAILED;
            } else {
                display("size of method list: " + methodList.size());
            }
        } catch(Exception e) {
            complain("Unexpected " + e);
            exitStatus = Consts.TEST_FAILED;
        }
        display("");

        display("getValue(null)");
        try {
            retValue = testedClass.getValue(null);
            complain("NullPointerException is not thrown");
            exitStatus = Consts.TEST_FAILED;
        } catch(NullPointerException e) {
            display("!!!expected NullPointerException");
        } catch(Exception e) {
            complain("Unexpected " + e);
            exitStatus = Consts.TEST_FAILED;
        }
        display("");

        display("getValues(null)");
        try {
            testedClass.getValues(null);
            complain("NullPointerException is not thrown");
            exitStatus = Consts.TEST_FAILED;
        } catch(NullPointerException e) {
            display("!!!expected NullPointerException");
        } catch(Exception e) {
            complain("Unexpected " + e);
            exitStatus = Consts.TEST_FAILED;
        }
        display("");

        List<Field> lst = null;
        display("getValues(list with size = 0)");
        try {
            testedClass.getValues(lst);
            complain("NullPointerException is not thrown");
            exitStatus = Consts.TEST_FAILED;
        } catch(NullPointerException e) {
            display("!!!expected NullPointerException");
        } catch(Exception e) {
            complain("Unexpected " + e);
            exitStatus = Consts.TEST_FAILED;
        }
        display("");

        display("equals(null)");
        try {
            if (testedClass.equals(null)) {
                complain("is equal to <null>");
                exitStatus = Consts.TEST_FAILED;
            } else {
                display("is not equal to <null>");
            }
        } catch(Exception e) {
            complain("Unexpected " + e);
            exitStatus = Consts.TEST_FAILED;
        }
        display("");

        display("=============");
        display("TEST FINISHES\n");

        debugee.resume();
        debugee.quit();
    }

}
