package org.wwhdrecomp.wwhd;

import java.io.File;

/** Debug-only authored input; inherits production scheduling, tools and activation checks. */
public final class SyntheticSetupService extends SetupService {
    @Override protected void executeBuild(File configuration) throws Exception {
        executePythonBuild("service_fixture", configuration);
    }
}
