# Editor manual screenshots

`manual.jsonl` is an editor transcript (see the automation manual) that takes the screenshots of the editor
manual in `docs/content/en/images/editor`. The CTest `docs.screenshots` (label `gpu`) runs it in a headless
editor with a GPU on a copy of `samples/showcase` (made by `CopyProject.cmake`) and compares every screenshot
with the committed image; CI runs it in the GPU job and uploads differing captures as `ui-captures`.

After an intended UI change, rewrite the images and review them in the pull request:

```bash
ILLUSION_UPDATE_GOLDENS=1 ctest --preset ci-linux-clang-debug -R docs.screenshots
```

To add a screenshot, add the steps that arrange the editor and an `editor.screenshot` step with an `image`
whose `golden` is the new file, then link it from both languages of the page.
