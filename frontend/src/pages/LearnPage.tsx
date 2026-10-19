import { PageHead } from '../components/Common';
import { useApp } from '../hooks/useApp';
import { LESSONS } from '../utils/learn';

export function LearnPage() {
  const { catalog } = useApp();
  return (
    <>
      <PageHead title="Learning Center"
        subtitle="The algorithms behind this project: what each does, why it was chosen, where it is used, how it works, and what it cannot do. For the algorithms the dashboard runs, complexities are read live from the C++ server." />
      <div className="stack">
        {LESSONS.map((lesson, index) => {
          const live = catalog.find((a) => a.id === lesson.id);
          return (
            <details key={lesson.id} className="card lesson" open={index === 0}>
              <summary>{lesson.title} <span className="pill" style={{ marginLeft: 8 }}>{lesson.paradigm}</span></summary>
              <h4>What it does</h4><p>{lesson.what}</p>
              <h4>Why it is used</h4><p>{lesson.why}</p>
              <h4>Where it is used in this project</h4><p>{lesson.where}</p>
              <h4>How it works</h4>
              <ol>{lesson.how.map((step, i) => <li key={i}>{step}</li>)}</ol>
              <h4>Pseudocode</h4><pre>{lesson.pseudocode}</pre>
              <h4>C++ in this project</h4><p>{lesson.cpp}</p>
              <h4>Small dry run</h4><p>{lesson.dryRun}</p>
              <h4>Complexity</h4>
              {live ? <p>Time <code>{live.complexity.time}</code> · Space <code>{live.complexity.space}</code> <span className="faint">(reported by the server)</span></p>
                : lesson.complexity ? <p>Time <code>{lesson.complexity.time}</code> · Space <code>{lesson.complexity.space}</code> <span className="faint">(analysis in docs/ALGORITHMS.md; not run from the dashboard)</span></p>
                : <p className="muted">Start the server to see the complexity reported by the implementation.</p>}
              {lesson.runIt && <><h4>Try it</h4><p><code>{lesson.runIt}</code></p></>}
              <h4>Limitations</h4><p>{lesson.limitations}</p>
            </details>
          );
        })}
      </div>
    </>
  );
}
