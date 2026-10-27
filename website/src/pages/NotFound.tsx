import { Link } from "react-router-dom";
import { Head } from "vite-react-ssg";

export function NotFound() {
  return (
    <div className="wrap notfound">
      <Head>
        <title>Not found · NightMare Network</title>
        <meta name="robots" content="noindex" />
      </Head>
      <h1>Not found</h1>
      <p>
        Nothing lives at this address. Try the <Link to="/docs/overview">documentation</Link> or the{" "}
        <Link to="/">front page</Link>.
      </p>
    </div>
  );
}
