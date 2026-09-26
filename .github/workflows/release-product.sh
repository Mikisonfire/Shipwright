#!/usr/bin/env bash
# SOH [fork] The product a release tag names, as key=value lines for $GITHUB_OUTPUT: the product
# (which picks .github/release-notes/<product>.md), the branch that builds it, and the release title.
# The one place generate-builds.yml maps a tag to its product.
set -euo pipefail

TAG="$1"
case "$TAG" in
  *unbound*)  echo "product=unbound";  echo "branch=unbound";                      echo "title=SoH: Unbound ${TAG}" ;;
  *celshade*) echo "product=celshade"; echo "branch=wind-waker-style-cel-shading"; echo "title=SoH (cel-shading fork) ${TAG}" ;;
  *) echo "tag ${TAG} names no product" >&2; exit 1 ;;
esac
