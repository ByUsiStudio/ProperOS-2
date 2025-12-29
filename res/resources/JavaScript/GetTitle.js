// 1. 检查常见的 meta 标签获取标题
let metaTitle = document.querySelector('meta[property="og:title"]')?.content ||
                document.querySelector('meta[name="twitter:title"]')?.content ||
                document.querySelector('meta[name="title"]')?.content;

if(metaTitle) {
    console.log("从meta标签找到标题:", metaTitle);
    // 尝试调用 iapp.fn
    iapp.fn("app.title(" + JSON.stringify(metaTitle) + ")");
} else {
    console.log("未在meta标签中找到标题");
}

// 2. 直接查看整个HTML文档的head部分，寻找线索
console.log("整个head的HTML内容:", document.head.innerHTML);